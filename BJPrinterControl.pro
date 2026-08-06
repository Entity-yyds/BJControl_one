QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

TARGET = BJPrinterControl
TEMPLATE = app
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

#源文件和头文件
SOURCES += \
    main.cpp \
    mainwindow.cpp\
    usbmanager.cpp

HEADERS += \
    mainwindow.h \
    usbmanager.h

FORMS += \
    mainwindow.ui

# 如果需要libusb或QtUsb，添加对应的库
# unix: LIBS += -lusb-1.0
# win32: LIBS += -lwinusb

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
