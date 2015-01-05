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
 * serial.cpp
 * Communicate with a CDC device using a serial port.
 * Copyright (C) 2014 Simon Newton
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <string>
#include <iostream>

using std::cerr;
using std::cout;
using std::endl;
using std::string;

// Use /dev/ttyACN0 on Linux
// Use "\\\\.\\USBSER000" or "\\\\.\\COM6" on Windows.
static const char kDevice[] = "/dev/cu.usbmodem1d11111";

int main() {
  int fd = open(kDevice, O_RDWR | O_NOCTTY);
  if (fd == -1) {
    cerr << "Failed to open " << kDevice << " : " << strerror(errno) << endl;
    return 1;
  }

  struct termios options;
  tcgetattr(fd, &options);
  options.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  options.c_oflag &= ~(ONLCR | OCRNL);
  int r = tcsetattr(fd, TCSANOW, &options);
  if (r) {
    cerr << "tcsetattr failed: " << strerror(errno) << endl;
    return 1;
  }

  while (true) {
    // TODO(simon): write the entire message here.
    const string request(
        "this is the request 1234567890 abcdefghijklmnopqrstuvwxyz");
    int r = write(fd, request.c_str(), request.size());
    if (r == -1) {
      cerr << "write() failed: " << strerror(errno) << endl;
      return -1;
    }

    char buffer[128];
    r = read(fd, buffer, sizeof(buffer));
    if (r < 0) {
      cerr << "Read failed: " << strerror(errno) << endl;
      break;
    }
    cout << "Got " << r << " bytes " << endl;
    const string response(buffer, r);
    cout << response << endl;
    sleep(1);
  }
  close(fd);
  return 0;
}
