/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * libusb.cpp
 * Communicate with a CDC style device using libusb.
 * Copyright (C) 2014 Simon Newton
 */

#include <errno.h>
#include <libusb.h>
#include <stdint.h>
#include <unistd.h>
#include <iostream>
#include <string>

using std::cerr;
using std::cout;
using std::endl;
using std::string;

static const uint16_t kProductId = 0x5252;
static const uint16_t kVendorId = 0x1d50;
static const uint8_t kInEndpoint  = 0x83;
static const uint8_t kOutEndpoint = 0x02;
static const unsigned int kReadTimeout = 1000;
static const uint8_t ACM_CTRL_DTR = 0x01;
static const uint8_t ACM_CTRL_RTS = 0x02;


/**
 * Write data to the device.
 */
bool Write(libusb_device_handle *device, const string &data) {
  int actual_length;
  int r = libusb_bulk_transfer(
      device, kOutEndpoint,
      // Terrible
      reinterpret_cast<uint8_t*>(const_cast<char*>(data.c_str())),
      data.size(), &actual_length, 0);
  if (r < 0) {
    cerr << "Build transfer failed: " << libusb_error_name(r) << endl;
    cerr << "Transferred " << actual_length << " / " << data.size() << " bytes"
         << endl;
    return false;
  }
  return true;
}

/**
 * Read data from the device
 */
int Read(libusb_device_handle *device, string *input, int size) {
  uint8_t data[size];
  int actual_length;
  int r = libusb_bulk_transfer(device, kInEndpoint, data, size, &actual_length,
                               kReadTimeout);
  if (r == LIBUSB_ERROR_TIMEOUT) {
    cerr << "Read timeout!" << endl;
    return -1;
  } else if (r < 0) {
    cerr << "Error while reading: " << libusb_error_name(r) << endl;
    return -1;
  }
  input->append(reinterpret_cast<const char*>(data), actual_length);
  return actual_length;
}

/**
 * @returns a new device, or NULL
 */
libusb_device_handle* OpenDevice(libusb_context *context) {
  // Look for a specific device and open it.
  struct libusb_device_handle *device = libusb_open_device_with_vid_pid(
      context, kVendorId, kProductId);
  if (!device) {
    cout << "Failed to open device: VID: 0x" << std::hex << kVendorId
         << ", PID: 0x" << std::hex << kProductId << endl;
    return NULL;
  }

  // A CDC device has a control and data interface.
  // Detatch the kernel driver from both.
  int r;
  for (int iface = 0; iface < 2; iface++) {
    if (libusb_kernel_driver_active(device, iface)) {
      r = libusb_detach_kernel_driver(device, iface);
      if (r < 0) {
        cout << "Failed to detach kernel driver: " << libusb_error_name(r)
             << endl;
        libusb_close(device);
        return NULL;
      }
    }
    r = libusb_claim_interface(device, iface);
    if (r < 0) {
      cout << "Failed to claim interface: " << libusb_error_name(r) << endl;
      libusb_close(device);
      return NULL;
    }
  }

  /* Start configuring the device:
   * - set line state
   */
  r = libusb_control_transfer(
      device, 0x21, 0x22, ACM_CTRL_DTR | ACM_CTRL_RTS, 0, NULL, 0, 0);
  if (r < 0) {
    cerr << "Control transferr failed: " << libusb_error_name(r) << endl;
    libusb_close(device);
    return NULL;
  }

  /* - set line encoding: here 9600 8N1
   * 9600 = 0x2580 ~> 0x80, 0x25 in little endian
   */
  uint8_t encoding[] = { 0x80, 0x25, 0x00, 0x00, 0x00, 0x00, 0x08 };
  r = libusb_control_transfer(
      device, 0x21, 0x20, 0, 0, encoding, sizeof(encoding), 0);
  if (r < 0) {
    cerr << "Control transfer 2 failed: " << libusb_error_name(r) << endl;
    libusb_close(device);
    return NULL;
  }
  return device;
}

int main(int argc, char **argv) {
  libusb_context *context = NULL;

  int r = libusb_init(&context);
  if (r < 0) {
    cerr << "libusb_init() failed: " << libusb_error_name(r) << endl;
    exit(1);
  }

  libusb_set_debug(context, 3);

  // Look for a specific device and open it.
  struct libusb_device_handle *device = OpenDevice(context);
  if (!device) {
    libusb_exit(context);
    exit(1);
  }

  unsigned char buf[65];
  int len;

  const string request = "abc";
  while (true) {
    Write(device, request);
    cout << "Wrote: '" << request << "'" << endl;
    sleep(1);
  }

  libusb_release_interface(device, 0);
  libusb_close(device);
  libusb_exit(context);
  return 0;
}
