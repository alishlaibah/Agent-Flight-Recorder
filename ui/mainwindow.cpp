#include "mainwindow.h"

#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QLabel>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <QLegend>
#include <QPainter>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Agent Flight Recorder");
    resize(800, 600);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *layout = new QVBoxLayout(central);

    // Top row: query input + record button.
    QHBoxLayout *topRow = new QHBoxLayout();
    m_queryInput = new QLineEdit();
    m_queryInput->setPlaceholderText("Enter query...");
    m_recordButton = new QPushButton("Record");
    topRow->addWidget(m_queryInput);
    topRow->addWidget(m_recordButton);
    layout->addLayout(topRow);

    // Chart row: live CPU and memory, side by side, fixed height.
    cpuSeries = new QLineSeries();
    cpuSeries->setColor(QColor("#e85c5c"));
    cpuChart = new QChart();
    cpuChart->addSeries(cpuSeries);
    cpuChart->legend()->setVisible(false);
    cpuAxisX = new QValueAxis(); cpuAxisX->setTitleText("Time (s)"); cpuAxisX->setRange(0, maxX);
    cpuAxisY = new QValueAxis(); cpuAxisY->setTitleText("CPU %");   cpuAxisY->setRange(0, 100);
    cpuChart->addAxis(cpuAxisX, Qt::AlignBottom);
    cpuChart->addAxis(cpuAxisY, Qt::AlignLeft);
    cpuSeries->attachAxis(cpuAxisX);
    cpuSeries->attachAxis(cpuAxisY);
    cpuChartView = new QChartView(cpuChart);
    cpuChartView->setRenderHint(QPainter::Antialiasing);

    memSeries = new QLineSeries();
    memSeries->setColor(QColor("#4a9edd"));
    memChart = new QChart();
    memChart->addSeries(memSeries);
    memChart->legend()->setVisible(false);
    memAxisX = new QValueAxis(); memAxisX->setTitleText("Time (s)");      memAxisX->setRange(0, maxX);
    memAxisY = new QValueAxis(); memAxisY->setTitleText("Memory (MB)");   memAxisY->setRange(0, maxMem);
    memChart->addAxis(memAxisX, Qt::AlignBottom);
    memChart->addAxis(memAxisY, Qt::AlignLeft);
    memSeries->attachAxis(memAxisX);
    memSeries->attachAxis(memAxisY);
    memChartView = new QChartView(memChart);
    memChartView->setRenderHint(QPainter::Antialiasing);

    QWidget *chartRow = new QWidget();
    QHBoxLayout *chartLayout = new QHBoxLayout(chartRow);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->addWidget(cpuChartView, 1);
    chartLayout->addWidget(memChartView, 1);
    chartRow->setFixedHeight(250);
    layout->addWidget(chartRow);

    // Live event list.
    layout->addWidget(new QLabel("Live events:"));
    m_eventList = new QListWidget();
    m_eventList->setFont(QFont("Monospace", 10));
    layout->addWidget(m_eventList, 3);

    // Saved sessions list.
    layout->addWidget(new QLabel("Saved sessions (double-click to open):"));
    m_sessionList = new QListWidget();
    layout->addWidget(m_sessionList, 1);

    connect(m_recordButton, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(m_sessionList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        openSession(m_sessionList->row(item));
    });

    loadSavedSessions();
}

void MainWindow::startRecording() {
    if (m_recorder && m_recorder->state() != QProcess::NotRunning) return;

    m_currentQuery = m_queryInput->text().trimmed();
    if (m_currentQuery.isEmpty()) return;

    m_eventList->clear();
    m_startTime = QDateTime::currentDateTime();

    // Reset chart state for a fresh session.
    cpuSeries->clear();
    memSeries->clear();
    maxX = 30.0;
    maxMem = 512.0;
    cpuAxisX->setRange(0, maxX);
    memAxisX->setRange(0, maxX);
    cpuAxisY->setRange(0, 100);
    memAxisY->setRange(0, maxMem);
    peakCpu = peakCpuTime = peakMem = peakMemTime = 0;
    peakCpuCause.clear();
    peakMemCause.clear();
    lastEventLine.clear();
    sessionTimer.start();

    m_recorder = new QProcess(this);
    connect(m_recorder, &QProcess::readyReadStandardOutput, this, &MainWindow::onRecorderOutput);
    connect(m_recorder, &QProcess::finished, this, &MainWindow::onRecorderFinished);

    // The recorder binary sits next to flight_ui in the build directory.
    QString recorderPath = QDir(QCoreApplication::applicationDirPath()).filePath("recorder");
    m_recorder->start(recorderPath, {m_currentQuery});

    m_recordButton->setEnabled(false);
}

void MainWindow::onRecorderOutput() {
    while (m_recorder->canReadLine()) {
        QString line = QString::fromUtf8(m_recorder->readLine()).trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("[STATS]")) {
            double cpu = 0, mem = 0;
            sscanf(line.toUtf8().constData(), "[STATS] cpu=%lf mem=%lf", &cpu, &mem);
            double t = sessionTimer.elapsed() / 1000.0;

            cpuSeries->append(t, cpu);
            memSeries->append(t, mem);

            if (t > maxX) {
                maxX = t + 5;
                cpuAxisX->setRange(0, maxX);
                memAxisX->setRange(0, maxX);
            }
            if (mem > maxMem) {
                maxMem = mem * 1.2;
                memAxisY->setRange(0, maxMem);
            }

            // Track peaks for spike attribution in the .md.
            if (cpu > peakCpu) {
                peakCpu = cpu;
                peakCpuTime = t;
                peakCpuCause = lastEventLine;
            }
            if (mem > peakMem) {
                peakMem = mem;
                peakMemTime = t;
                peakMemCause = lastEventLine;
            }
            continue;  // do NOT add [STATS] to the events list
        }

        // Remember the most recent file/command event for spike attribution.
        if (line.startsWith("[OPEN]") || line.startsWith("[EXEC]"))
            lastEventLine = line;

        QListWidgetItem *item = new QListWidgetItem(line);

        if (line.startsWith("[SIGNAL]"))
            item->setForeground(Qt::red);
        else if (line.startsWith("[EXEC]"))
            item->setForeground(Qt::blue);
        // [OPEN] and anything else get the default colour.

        m_eventList->addItem(item);
        m_eventList->scrollToBottom();
    }
}

void MainWindow::onRecorderFinished(int /*exitCode*/, QProcess::ExitStatus /*status*/) {
    saveSession();
    loadSavedSessions();
    m_recordButton->setEnabled(true);
    m_recorder->deleteLater();
    m_recorder = nullptr;
}

void MainWindow::saveSession() {
    QDir().mkpath(sessionDir());

    // Build a slug from the first few words of the query.
    QString slug = m_currentQuery.toLower()
                       .replace(QRegularExpression("[^a-z0-9]+"), "-")
                       .left(40)
                       .trimmed();

    QString timestamp = m_startTime.toString("yyyy-MM-dd_HH-mm-ss");
    QString filename  = sessionDir() + "/" + timestamp + "_" + slug + ".md";

    // Count event types for the summary.
    int opens = 0, execs = 0, sigs = 0;
    QStringList signalNames;
    QStringList allLines;

    for (int i = 0; i < m_eventList->count(); i++) {
        QString line = m_eventList->item(i)->text();
        allLines.append(line);
        if      (line.startsWith("[OPEN]"))   opens++;
        else if (line.startsWith("[EXEC]"))   execs++;
        else if (line.startsWith("[SIGNAL]")) {
            sigs++;
            // Extract signal name — format is "[SIGNAL] SIGSTOP pid=..."
            QStringList parts = line.split(' ');
            if (parts.size() >= 2) signalNames.append(parts[1]);
        }
    }

    double durationSecs = m_startTime.msecsTo(QDateTime::currentDateTime()) / 1000.0;

    QString signalSummary = QString::number(sigs);
    if (!signalNames.isEmpty()) signalSummary += " (" + signalNames.join(", ") + ")";

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out << "# Session: " << m_currentQuery << "\n\n";
    out << "**Started:** " << m_startTime.toString("yyyy-MM-dd HH:mm:ss") << "\n";
    out << "**Duration:** " << QString::number(durationSecs, 'f', 1) << "s\n\n";
    out << "## Summary\n\n";
    out << "- File opens: " << opens << "\n";
    out << "- Commands: " << execs << "\n";
    out << "- Signals: " << signalSummary << "\n\n";
    if (peakCpu > 0)
        out << "**Peak CPU:** " << QString::number(peakCpu, 'f', 1) << "% at "
            << QString::number(peakCpuTime, 'f', 1) << "s — caused by `"
            << peakCpuCause << "`\n";
    if (peakMem > 0)
        out << "**Peak memory:** " << QString::number(peakMem, 'f', 0) << " MB at "
            << QString::number(peakMemTime, 'f', 1) << "s — caused by `"
            << peakMemCause << "`\n";
    if (peakCpu > 0 || peakMem > 0) out << "\n";
    out << "## Timeline\n\n";
    for (const QString &line : allLines) out << line << "\n";
}

void MainWindow::loadSavedSessions() {
    m_sessionList->clear();

    QDir dir(sessionDir());
    if (!dir.exists()) return;

    QStringList files = dir.entryList({"*.md"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &name : files) {
        QListWidgetItem *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, dir.filePath(name));
        m_sessionList->addItem(item);
    }
}

void MainWindow::openSession(int row) {
    QListWidgetItem *item = m_sessionList->item(row);
    if (!item) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));
}

QString MainWindow::sessionDir() const {
    return QCoreApplication::applicationDirPath() + "/sessions";
}
