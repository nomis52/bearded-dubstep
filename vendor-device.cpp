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
 * vendor-device.cpp
 * Communicate with a custom vendor device using libusb.
 * Copyright (C) 2015 Simon Newton
 */

#include <errno.h>
#include <libusb.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <queue>
#include <string>

using std::cerr;
using std::cout;
using std::setw;
using std::ostream;
using std::endl;
using std::string;

static const uint16_t kProductId = 0x0053;
static const uint16_t kVendorId = 0x04d8;
static const uint8_t kInEndpoint  = 0x81;
static const uint8_t kOutEndpoint = 0x01;
static const unsigned int kTimeout = 1000;

template <typename T, size_t N>
  char (&ArraySizeHelper(T (&array)[N]))[N];

template <typename T, size_t N>
  char (&ArraySizeHelper(const T (&array)[N]))[N];

ostream& operator<<(std::ostream &out, const struct timeval &tv) {
  return out << std::dec << tv.tv_sec << "." << std::setw(6)
             << std::setfill('0') << tv.tv_usec;
}

#define arraysize(array) (sizeof(ArraySizeHelper(array)))

class LibUsbThread;

void *StartThread(void *d);

class LibUsbThread {
 public:
  explicit LibUsbThread(libusb_context *context)
      : m_context(context),
        m_thread_id(),
        m_terminate(false),
        m_devices(0) {
    pthread_mutex_init(&m_mutex, NULL);
  }

  ~LibUsbThread() {
    cout << m_devices << " devices remain in use" << endl;
    pthread_mutex_destroy(&m_mutex);
  }

  int OpenDevice(libusb_device *dev, libusb_device_handle **handle) {
    int r = libusb_open(dev, handle);
    if (r == 0) {
      m_devices++;
      if (m_devices == 1) {
        int ret = pthread_create(&m_thread_id, NULL, StartThread,
                                 static_cast<void*>(this));
        if (ret) {
          cerr << "Failed to start thread" << endl;
        }
      }
      cout << "Opened USB device " << handle << endl;
    }
    return r;
  }

  void CloseDevice(libusb_device_handle *handle) {
    cout << "Closing device " << handle << endl;

    if (m_devices > 0) {
      pthread_mutex_lock(&m_mutex);
      m_terminate = true;
      pthread_mutex_unlock(&m_mutex);
    }
    libusb_close(handle);
    m_devices--;
    if (m_devices == 0) {
      cout << "Waiting for libusb thread..." << endl;
      pthread_join(m_thread_id, NULL);
    }
  }

  void *_InternalRun() {
    while (true) {
      pthread_mutex_lock(&m_mutex);
      if (m_terminate) {
        return NULL;
      }
      pthread_mutex_unlock(&m_mutex);
      libusb_handle_events(m_context);
    }
  }

 private:
  libusb_context *m_context;
  pthread_t m_thread_id;
  pthread_mutex_t m_mutex;
  bool m_terminate;  // GUARDED_BY(m_mutex);
  unsigned int m_devices;
};


void InTransferCompleteHandler(struct libusb_transfer *transfer);
void OutTransferCompleteHandler(struct libusb_transfer *transfer);

class UsbSender {
 public:
  explicit UsbSender(libusb_device_handle *device)
      : m_device(device),
        m_in_transfer(NULL),
        m_out_transfer(NULL),
        m_got_response(false) {
    pthread_mutex_init(&m_mutex, NULL);
    pthread_cond_init(&m_condition, NULL);

    m_in_transfer = libusb_alloc_transfer(0);
    m_out_transfer = libusb_alloc_transfer(0);
  }

  ~UsbSender() {
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_condition);
  }

  bool SendRequest(uint16_t command, const uint8_t *data, unsigned int size) {
    if (size > MAX_MESSAGE_SIZE) {
      cerr << "Message exceeds max size" << endl;
      return false;
    }

    unsigned int offset = 0;
    m_out_buffer[0] = SOF_IDENTIFIER;
    m_out_buffer[1] = static_cast<uint8_t>(command & 0xff);
    m_out_buffer[2] = static_cast<uint8_t>(command >> 8);
    m_out_buffer[3] = static_cast<uint8_t>(size & 0xff);
    m_out_buffer[4] = static_cast<uint8_t>(size >> 8);
    offset += 5;

    if (size > 0) {
      memcpy(m_out_buffer + offset, data, size);
      offset += size;
    }
    m_out_buffer[offset++] = EOF_IDENTIFIER;

    if (offset % MAX_PACKET_SIZE == 0)  {
      // We need to pad the messaeg so that the transfer completes at the PIC
      // end. We could use LIBUSB_TRANSFER_ADD_ZERO_PACKET instead.
      m_out_buffer[offset++] = 0;
    }

    libusb_fill_bulk_transfer(m_out_transfer, m_device, kOutEndpoint,
                              m_out_buffer, offset,
                              OutTransferCompleteHandler,
                              static_cast<void*>(this),
                              kTimeout);
    gettimeofday(&m_send_out_time, NULL);
    cout << "Sending " << std::dec << offset << " bytes at "
         << m_send_out_time << endl;

    int r = libusb_submit_transfer(m_out_transfer);
    if (r) {
      cerr << "Failed to submit out transfer" << endl;
    }
    return r == 0;
  }

  void _OutTransferComplete() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cout << "Out transfer completed at " << tv << ", status is "
         << libusb_error_name(m_out_transfer->status) << endl;
    if (m_out_transfer->status == LIBUSB_TRANSFER_COMPLETED) {
      cout << "Sent " << m_out_transfer->actual_length << " bytes" << endl;
      SubmitInTransfer();
    }
  }

  void _InTransferComplete() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cout << "In transfer completed, at " << tv << ", status is "
         << libusb_error_name(m_in_transfer->status) << endl;
    if (m_in_transfer->status == LIBUSB_TRANSFER_COMPLETED) {
      cout << "Got " << std::dec << m_in_transfer->actual_length << " bytes"
           << endl;
      cout << "Received: " << std::hex;
      for (unsigned int i = 0; i < m_in_transfer->actual_length; i++) {
        cout << std::setw(2) << std::setfill('0')
             << static_cast<int>(m_in_transfer->buffer[i]) << " ";
      }
      cout << endl;
    }
    struct timeval diff;
    timersub(&tv, &m_send_out_time, &diff);
    cout << "Total time was " << diff << endl;
    pthread_mutex_lock(&m_mutex);
    m_got_response = true;
    pthread_mutex_unlock(&m_mutex);
    pthread_cond_signal(&m_condition);
  }

  void Wait() {
    pthread_mutex_lock(&m_mutex);
    if (m_got_response) {
      pthread_mutex_unlock(&m_mutex);
      return;
    } else {
      pthread_cond_wait(&m_condition, &m_mutex);
      pthread_mutex_unlock(&m_mutex);
    }
  }

  enum {
    ECHO_COMMAND = 0x80,
    TX_DMX = 0x81
  };

 private:
  // Should be a multiple of the endpoint packet size to avoid libusb overflows.
  enum {
    IN_BUFFER_SIZE = 1024
  };

  enum {
    OUT_BUFFER_SIZE = 1024
  };

  uint8_t m_in_buffer[IN_BUFFER_SIZE];
  uint8_t m_out_buffer[OUT_BUFFER_SIZE];

  libusb_device_handle *m_device;
  libusb_transfer *m_out_transfer;
  libusb_transfer *m_in_transfer;
  std::queue<string> m_messages;
  pthread_mutex_t m_mutex;
  pthread_cond_t m_condition;
  bool m_got_response;
  struct timeval m_send_out_time;
  struct timeval m_send_in_time;

  void SubmitInTransfer() {
    libusb_fill_bulk_transfer(m_in_transfer, m_device, kInEndpoint,
                                   m_in_buffer, IN_BUFFER_SIZE,
                                   InTransferCompleteHandler,
                                   static_cast<void*>(this),
                                   kTimeout);
    gettimeofday(&m_send_in_time, NULL);
    int r = libusb_submit_transfer(m_in_transfer);
    if (r) {
      cerr << "Failed to submit input transfer" << endl;
    }
    cout << "Submitted in transfer at " << m_send_in_time << endl;
  }

  static const uint8_t SOF_IDENTIFIER = 0x5a;
  static const uint8_t EOF_IDENTIFIER = 0xa5;
  static const unsigned int MAX_MESSAGE_SIZE = 513;
  static const unsigned int MAX_PACKET_SIZE = 64;
};

void InTransferCompleteHandler(struct libusb_transfer *transfer) {
  UsbSender *sender = static_cast<UsbSender*>(transfer->user_data);
  return sender->_InTransferComplete();
}

void OutTransferCompleteHandler(struct libusb_transfer *transfer) {
  UsbSender *sender = static_cast<UsbSender*>(transfer->user_data);
  return sender->_OutTransferComplete();
}

void *StartThread(void *d) {
  LibUsbThread *thread = static_cast<LibUsbThread*>(d);
  return thread->_InternalRun();
}

bool IsInteresting(libusb_device *device) {
  struct libusb_device_descriptor device_descriptor;
  libusb_get_device_descriptor(device, &device_descriptor);

  cout << "Checking vendor 0x" << std::hex << std::setw(4) << std::setfill('0')
       << device_descriptor.idVendor << ", product 0x" << std::setw(4)
       << device_descriptor.idProduct << endl;

  return device_descriptor.idVendor == kVendorId &&
         device_descriptor.idProduct == kProductId;
}

libusb_device_handle* LocateDevice(LibUsbThread *thread,
                                   libusb_context *context) {
  libusb_device **list;
  libusb_device_handle *handle = NULL;
  ssize_t cnt = libusb_get_device_list(context, &list);
  if (cnt < 0) {
    cerr << "libusb_free_device_list failed" << endl;
    return NULL;
  }

  int err = 0;
  libusb_device *found = NULL;
  for (int i = 0; i < cnt; i++) {
    libusb_device *device = list[i];
    if (IsInteresting(device)) {
      found = device;
      break;
    }
  }

  if (found) {
    err = thread->OpenDevice(found, &handle);
    if (err) {
      cerr << "libusb_open failed" << endl;
    }
  }
  libusb_free_device_list(list, 1);
  return handle;
}

int main(int argc, char **argv) {
  libusb_context *context = NULL;

  int r = libusb_init(&context);
  if (r < 0) {
    cerr << "libusb_init() failed: " << libusb_error_name(r) << endl;
    exit(1);
  }

  libusb_set_debug(context, 3);

  LibUsbThread thread(context);

  // Look for a specific device and open it.
  struct libusb_device_handle *device = LocateDevice(&thread, context);
  if (!device) {
    libusb_exit(context);
    exit(1);
  }


  r = libusb_claim_interface(device, 0);
  if (r) {
    cerr << "Failed to claim interface: 0" << endl;
    exit(1);
  }

  UsbSender sender(device);

  while (1) {
    uint8_t request[] = {1, 2, 3};

    if (!sender.SendRequest(UsbSender::TX_DMX, request,
                            arraysize(request))) {
      break;
    }
    sender.Wait();
    sleep(1);
    break;
  }

  r = libusb_release_interface(device, 0);
  thread.CloseDevice(device);
  libusb_exit(context);
  return 0;
}
