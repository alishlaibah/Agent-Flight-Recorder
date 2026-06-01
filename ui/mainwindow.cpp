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
    return QDir::homePath() + "/.agent-flight-recorder/sessions";
}
