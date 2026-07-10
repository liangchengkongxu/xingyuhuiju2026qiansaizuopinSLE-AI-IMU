# Makefile for SS928 Widgets panel（板端 g++ + 系统 Qt5.15 + Multimedia）
SYSQT   = /usr/lib/aarch64-linux-gnu
SYSINC  = /usr/include/aarch64-linux-gnu/qt5
CXX     = g++
MOC     = /usr/bin/moc
IMU_DIR = imucnn
NPU_LIB = /opt/lib/npu
IMU_CNN_LIB = lib/libimu_cnn.a

INCLUDES = -I. -I$(IMU_DIR) -I$(SYSINC) \
           -I$(SYSINC)/QtWidgets \
           -I$(SYSINC)/QtGui \
           -I$(SYSINC)/QtCore \
           -I$(SYSINC)/QtMultimedia \
           -I$(SYSINC)/QtMultimediaWidgets \
           -I$(SYSINC)/QtNetwork
CXXFLAGS = -std=gnu++17 -Wall -O2 -D_REENTRANT -fPIC $(INCLUDES)
LDFLAGS  = -L$(SYSQT) -Wl,-rpath,$(SYSQT) -L$(NPU_LIB) -Wl,-rpath,$(NPU_LIB)
LDLIBS   = -lQt5MultimediaWidgets -lQt5Multimedia -lQt5Widgets -lQt5Gui -lQt5Core -lQt5Network -lpthread

ifneq (,$(wildcard $(IMU_CNN_LIB)))
  LDLIBS += $(IMU_CNN_LIB) -lascendcl
  CXXFLAGS += -DWIDGET_IMU_CNN=1
endif

TARGET   = widget_panel
UI_SRC   = ui_common.cpp pages_home.cpp pages_practice.cpp pages_training.cpp \
           pages_match.cpp pages_class.cpp main_window.cpp
SRC      = main.cpp $(UI_SRC) sle_imu_service.cpp sle_seek_service.cpp \
           imu_swing_detector.cpp imu_cnn_classifier.cpp widget_yolo_action.cpp \
           class_hit_ai_advice.cpp cloud_upload_service.cpp
OBJ      = $(SRC:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@.tmp $(OBJ) $(LDLIBS)
	test -s $@.tmp
	mv -f $@.tmp $@

ui_pages.moc: ui_pages.h ui_common.h
	$(MOC) ui_pages.h -o ui_pages.moc

main_window.moc: main_window.h ui_pages.h ui_common.h
	$(MOC) main_window.h -o main_window.moc

main.o: main.cpp ui_common.h main_window.h
	$(CXX) $(CXXFLAGS) -c -o $@ main.cpp

ui_common.o: ui_common.cpp ui_pages.moc ui_common.h ui_pages.h
	$(CXX) $(CXXFLAGS) -c -o $@ ui_common.cpp

pages_home.o: pages_home.cpp ui_common.h ui_pages.h main_window.h
	$(CXX) $(CXXFLAGS) -c -o $@ pages_home.cpp

pages_practice.o: pages_practice.cpp ui_common.h ui_pages.h main_window.h
	$(CXX) $(CXXFLAGS) -c -o $@ pages_practice.cpp

pages_training.o: pages_training.cpp ui_common.h ui_pages.h main_window.h
	$(CXX) $(CXXFLAGS) -c -o $@ pages_training.cpp

pages_match.o: pages_match.cpp ui_common.h ui_pages.h main_window.h
	$(CXX) $(CXXFLAGS) -c -o $@ pages_match.cpp

pages_class.o: pages_class.cpp ui_common.h ui_pages.h main_window.h
	$(CXX) $(CXXFLAGS) -c -o $@ pages_class.cpp

main_window.o: main_window.cpp main_window.moc main_window.h ui_pages.h ui_common.h
	$(CXX) $(CXXFLAGS) -c -o $@ main_window.cpp

widget_yolo_action.moc: widget_yolo_action.h
	$(MOC) widget_yolo_action.h -o widget_yolo_action.moc

widget_yolo_action.o: widget_yolo_action.cpp widget_yolo_action.h widget_yolo_action.moc
	$(CXX) $(CXXFLAGS) -c -o $@ widget_yolo_action.cpp

sle_imu_service.moc: sle_imu_service.h
	$(MOC) sle_imu_service.h -o sle_imu_service.moc

sle_imu_service.o: sle_imu_service.cpp sle_imu_service.h sle_imu_service.moc imu_swing_detector.h imu_cnn_classifier.h
	$(CXX) $(CXXFLAGS) -c -o $@ sle_imu_service.cpp

sle_seek_service.moc: sle_seek_service.h
	$(MOC) sle_seek_service.h -o sle_seek_service.moc

sle_seek_service.o: sle_seek_service.cpp sle_seek_service.h sle_seek_service.moc
	$(CXX) $(CXXFLAGS) -c -o $@ sle_seek_service.cpp

imu_swing_detector.o: imu_swing_detector.cpp imu_swing_detector.h
	$(CXX) $(CXXFLAGS) -c -o $@ imu_swing_detector.cpp

imu_cnn_classifier.o: imu_cnn_classifier.cpp imu_cnn_classifier.h
	$(CXX) $(CXXFLAGS) -c -o $@ imu_cnn_classifier.cpp

class_hit_ai_advice.o: class_hit_ai_advice.cpp class_hit_ai_advice.h
	$(CXX) $(CXXFLAGS) -c -o $@ class_hit_ai_advice.cpp

cloud_upload_service.moc: cloud_upload_service.h
	$(MOC) cloud_upload_service.h -o cloud_upload_service.moc

cloud_upload_service.o: cloud_upload_service.cpp cloud_upload_service.h cloud_upload_service.moc
	$(CXX) $(CXXFLAGS) -c -o $@ cloud_upload_service.cpp

clean:
	rm -f $(TARGET) $(TARGET).tmp $(OBJ) ui_pages.moc main_window.moc \
		sle_imu_service.moc sle_seek_service.moc widget_yolo_action.moc cloud_upload_service.moc
