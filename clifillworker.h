#ifndef CLIFILLWORKER_H
#define CLIFILLWORKER_H

#include <QObject>
#include <QString>

class CliFillWorker : public QObject
{
    Q_OBJECT
public:
    CliFillWorker(const QString &inputPath, const QString &outputPath,
                  double dpi, QObject *parent = nullptr);

public slots:
    void run();

signals:
    void finished(int status, int layerCount, int width, int height, int rowBytes);

private:
    QString inputPath;
    QString outputPath;
    double dpi;
};

#endif
