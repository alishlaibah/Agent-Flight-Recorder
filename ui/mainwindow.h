#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QDateTime>
#include <QElapsedTimer>

class QLineEdit;
class QPushButton;
class QListWidget;
class QChart;
class QChartView;
class QLineSeries;
class QValueAxis;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void startRecording();
    void onRecorderOutput();
    void onRecorderFinished(int exitCode, QProcess::ExitStatus status);
    void openSession(int row);

private:
    void loadSavedSessions();
    void saveSession();
    QString sessionDir() const;

    QLineEdit    *m_queryInput;
    QPushButton  *m_recordButton;
    QListWidget  *m_eventList;
    QListWidget  *m_sessionList;

    QProcess    *m_recorder = nullptr;
    QDateTime    m_startTime;
    QString      m_currentQuery;

    // Live resource charts (fed by [STATS] events).
    QChart      *cpuChart, *memChart;
    QChartView  *cpuChartView, *memChartView;
    QLineSeries *cpuSeries, *memSeries;
    QValueAxis  *cpuAxisX, *cpuAxisY, *memAxisX, *memAxisY;
    QElapsedTimer sessionTimer;
    double maxX = 30.0;
    double maxMem = 512.0;
    double peakCpu = 0, peakCpuTime = 0;
    double peakMem = 0, peakMemTime = 0;
    QString peakCpuCause, peakMemCause;
    QString lastEventLine;  // most recent [OPEN]/[EXEC] line, for spike attribution
};
