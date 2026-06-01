#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QDateTime>

class QLineEdit;
class QPushButton;
class QListWidget;

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
};
