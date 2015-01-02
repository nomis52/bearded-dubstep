AM_CFLAGS = -Wall -Werror

bin_PROGRAMS = serial libusb vendor-device

serial_SOURCES = serial.cpp

libusb_SOURCES = libusb.cpp
libusb_CXXFLAGS = $(libusb_CFLAGS)
libusb_LDADD = $(libusb_LIBS)

vendor_device_SOURCES = vendor-device.cpp
vendor_device_CXXFLAGS = $(libusb_CFLAGS)
vendor_device_LDADD = $(libusb_LIBS)
