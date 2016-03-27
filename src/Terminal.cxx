/*
Copyright (c) 2016 Joe Davisson.

This file is part of EasySXB.

EasySXB is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

EasySXB is distributed in the hope that it will be useful,
state WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with EasySXB; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
*/

#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#ifndef WIN32
  #include <unistd.h>
  #include <string.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <fcntl.h>
  #include <termios.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>

#include "Dialog.H"
#include "Gui.H"
#include "Terminal.H"

namespace
{
  bool connected = false;
  int flash;
  char buf[4096];
  int buf_pos;

#ifdef WIN32
  HANDLE hserial;
  DCB dcb;
  COMMTIMEOUTS timeouts;
#else
  struct termios term;
  int fd;
  struct timeval tv;
#endif

  // store previous directory paths
  char load_dir[256];

  // extract directory from a path/filename string
  void getDirectory(char *dest, const char *src)
  {
    strcpy(dest, src);

    int len = strlen(dest);
    if(len < 2)
      return;

    for(int i = len - 1; i > 0; i--)
    {
      if(dest[i - 1] == '/')
      {
        dest[i] = '\0';
        break;
      }
    }
  }

  void delay(int ms)
  {
#ifdef WIN32
//    Sleep(ms);
#else
//    usleep(ms * 1000);
#endif
  }
}

void Terminal::connect(const char *device)
{
#ifdef WIN32
  hserial = CreateFile(device, GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);

  if(hserial == INVALID_HANDLE_VALUE)
  {
    Dialog::message("Error", "Could not open serial port.");
    return;
  }

  GetCommState(hserial, &dcb);
  dcb.BaudRate = CBR_9600;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;

  if(SetCommState(hserial, &dcb) == 0)
  {
    CloseHandle(hserial);
    Dialog::message("Error", "Could not open serial port.");
    return;
  }

  memset(&timeouts, 0, sizeof(timeouts));
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 0;
  timeouts.WriteTotalTimeoutMultiplier = 0;

  if(SetCommTimeouts(hserial, &timeouts) == 0)
  {
    CloseHandle(hserial);
    Dialog::message("Error", "Could not open serial port.");
    return;
  }
#else
  fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);

  if(fd == -1)
  {
    Dialog::message("Error", "Could not open serial port.");
    return;
  }

  memset(&term, 0, sizeof(term));
  term.c_cflag = B9600 | CS8 | CREAD| CLOCAL;
  term.c_iflag = IGNPAR;
  term.c_oflag = 0;
  term.c_lflag = 0;
  term.c_cc[VTIME] = 2;
  term.c_cc[VMIN] = 0;
  tcsetattr(fd, TCSANOW, &term);

  tv.tv_sec = 0;
  tv.tv_usec = 100000;
#endif

  flash = 0;
  connected = true;

  Gui::append("\n(Connected to SXB at 9600 baud.)\n");
  delay(500);
}

void Terminal::disconnect()
{
  if(connected == true)
  {
#ifdef WIN32
    CloseHandle(hserial);
#else
    close(fd);
#endif
    connected = false;
    Gui::append("\n(Connection Closed.)\n");
    Dialog::message("Disconnected", "Connection Closed.");
  }
}

bool Terminal::isConnected()
{
  return connected;
}

void Terminal::sendChar(char c)
{
#ifdef WIN32
  DWORD bytes;

  if(connected == true)
  {
    // convert to uppercase so it looks nice then the SXB echos the character
    c = toupper(c);

    // convert carriage return
    if(c == '\n')
      c = 13;

    WriteFile(hserial, &c, 1, &bytes, NULL);
    delay(16);
  }
#else
  if(connected == true)
  {
    // convert to uppercase so it looks nice then the SXB echos the character
    c = toupper(c);

    // convert carriage return
    if(c == '\n')
      c = 13;

    int temp = write(fd, &c, 1);
    delay(16);
  }
#endif
}

char Terminal::getChar()
{
  char c;
  int tries = 0;

#ifdef WIN32
  DWORD bytes;

  if(connected == true)
  {
    while(1)
    {
      BOOL temp = ReadFile(hserial, &c, 1, &bytes, NULL);

      if(temp == 0 || bytes == 0)
        return -1;
      else
        return c;
    }
  }
#else
  if(connected == true)
  {
    while(1)
    {
      int temp = read(fd, &c, 1);

      if(temp <= 0)
        return -1;
      else
        return c;
    }
  }
#endif
}

void Terminal::sendString(const char *s)
{
  if(connected == true)
  {
    memset(buf, 0, sizeof(buf));
    strncpy(buf, s, strlen(s));

    for(int i = 0; i < strlen(buf); i++)
    {
      buf[i] = toupper(buf[i]);

      if(buf[i] == '\n')
        buf[i] = 13;
    }

#ifdef WIN32
    DWORD bytes;

    WriteFile(hserial, buf, strlen(buf), &bytes, NULL);
    delay(16);
#else
    int temp = write(fd, buf, strlen(buf));
    delay(16);
#endif
  }
}

void Terminal::getResult(char *s)
{
  if(connected == true)
  {
    getData();
    int j = 0;

    for(int i = 0; i < strlen(buf); i++)
    {
      char c = buf[i];

      if(c >= '0' && c <= '9' || c >= 'A' && c <= 'Z' || c == ' ')
        s[j++] = c;
    }

    s[j] = '\0';
  }
}

void Terminal::getData()
{
  memset(buf, 0, sizeof(buf));
  buf_pos = 0;

#ifdef WIN32
  DWORD bytes;

  if(connected == true)
  {
    while(1)
    {
      BOOL temp = ReadFile(hserial, buf + buf_pos, 256, &bytes, NULL);
      delay(16);

      if(temp == 0 || bytes == 0 || bytes > 32)
        break;

      buf_pos += bytes;
      if(buf_pos > 3500)
        break;
    }
  }
#else
  int bytes;

  if(connected == true)
  {
    while(1)
    {
      bytes = read(fd, buf + buf_pos, 256);
      delay(16);

      if(bytes <= 0 || bytes > 32)
        break;

      buf_pos += bytes;
      if(buf_pos > 3500)
        break;
    }
  }
#endif

  for(int i = 0; i < sizeof(buf); i++)
    if(buf[i] == 13)
      buf[i] = '\n';
}

void Terminal::receive(void *data)
{
  getData();
  Gui::append(buf);

  // cause cursor to flash
  flash++;

  if(flash > 64)
    flash = 0;

  Gui::flashCursor((((flash >> 2) & 1) == 1) ? true : false);

  Fl::repeat_timeout(.1, Terminal::receive, data);
}

void Terminal::changeReg(int reg, int num)
{
  if(connected == false)
    return;

  char s[256];

  if(Gui::getMode() == Gui::MODE_265)
  {
    switch(reg)
    {
      case REG_PC:
        sprintf(s, "|P%02X:%04X", num >> 16, num & 0xFFFF);
        sendString(s);
        break;
      case REG_A:
        sprintf(s, "|A%04X", num);
        sendString(s);
        break;
      case REG_X:
        sprintf(s, "|X%04X", num);
        sendString(s);
        break;
      case REG_Y:
        sprintf(s, "|Y%04X", num);
        sendString(s);
        break;
      case REG_SP:
        sprintf(s, "|S%04X", num);
        sendString(s);
        break;
      case REG_DP:
        sprintf(s, "|D%04X", num);
        sendString(s);
        break;
      case REG_SR:
        sprintf(s, "|F%02X", num);
        sendString(s);
        break;
      case REG_DB:
        sprintf(s, "|B%02X", num);
        sendString(s);
        break;
    }

    sendString("R");

    if(reg == REG_SR)
      Gui::setToggles(num);
  }
  else if(Gui::getMode() == Gui::MODE_134)
  {
    switch(reg)
    {
      case REG_PC:
        sprintf(s, "A%04X     ", num & 0xFFFF);
        sendString(s);
        break;
      case REG_SR:
        sprintf(s, "A %02X    ", num & 0xFF);
        sendString(s);
        break;
      case REG_A:
        sprintf(s, "A  %02X   ", num);
        sendString(s);
        break;
      case REG_X:
        sprintf(s, "A   %02X  ", num);
        sendString(s);
        break;
      case REG_Y:
        sprintf(s, "A    %02X ", num);
        sendString(s);
        break;
      case REG_SP:
        sprintf(s, "A     %02X", num);
        sendString(s);
        break;
    }

    sendString("R");

    if(reg == REG_SR)
      Gui::setToggles(num);
  }
}

void Terminal::updateRegs()
{
  if(connected == false)
    return;

  char s[64];
  memset(s, 0, sizeof(s));
  delay(1000);

  if(Gui::getMode() == Gui::MODE_265)
  {
    sendString("| ");
    getResult(s);
    Gui::updateRegs(s);
  }
  else if(Gui::getMode() == Gui::MODE_134)
  {
    sendString("R");
    getResult(s);
    Gui::updateRegs(s);
  }
}

void Terminal::jml(int address)
{
  if(connected == false)
    return;

  char s[256];

  if(Gui::getMode() == Gui::MODE_265)
  {
    sprintf(s, "G%02X%04X", address >> 16, address & 0xFFFF);
    sendString(s);
  }
  else if(Gui::getMode() == Gui::MODE_134)
  {
    sprintf(s, "G%04X", address & 0xFFFF);
    sendString(s);
  }
}

void Terminal::jsl(int address)
{
  if(connected == false)
    return;

  char s[256];

  if(Gui::getMode() == Gui::MODE_265)
  {
    sprintf(s, "J%02X%04X", address >> 16, address & 0xFFFF);
    sendString(s);
  }
  else if(Gui::getMode() == Gui::MODE_134)
  {
    sprintf(s, "J%04X", address & 0xFFFF);
    sendString(s);
  }
}

void Terminal::upload()
{
  if(connected == false)
  {
    Dialog::message("Error", "Not Connected.");
    return;
  }

  Fl_Native_File_Chooser fc;
  fc.title("Upload Program");
  fc.filter("HEX File\t*.hex\n");
  fc.options(Fl_Native_File_Chooser::PREVIEW);
  fc.type(Fl_Native_File_Chooser::BROWSE_FILE);
  fc.directory(load_dir);

  switch(fc.show())
  {
    case -1:
    case 1:
      return;
    default:
      getDirectory(load_dir, fc.filename());
      break;
  }

  int segment = 0;
  int address = 0;
  int code = 0;
  int value = 0;
  int count = 0;
  int temp;
  int ret;
  char s[256];

  FILE *fp = fopen(fc.filename(), "r");
  if(!fp)
    return;

  while(1)
  {
    temp = fgetc(fp);
    if(temp == EOF)
      break;

    // start of line
    if(temp == ':')
    {
      segment = 0;
      ret = fscanf(fp, "%02X", &count);

      // last line
      if(count == 0x00)
      {
        break;
      }
      else
      {
        ret = fscanf(fp, "%04X", &address);
        ret = fscanf(fp, "%02X", &code);

        if(code == 0x04)
        {
          ret = segment = address;
        }
        else if(code == 0x00)
        {
          int checksum = 0;

          // address
          sprintf(s, "S2%02X%02X%02X%02X",
                  count + 4, segment, address >> 8, address & 0xFF);
          sendString(s);
          checksum += count + 4;
          checksum += address >> 8;
          checksum += address & 0xFF;

          // data
          int index = 0;
          for(int i = 0; i < count; i++)
          {
            ret = fscanf(fp, "%02X", &value);
            sprintf(s + index, "%02X", value);
            index += 2;
            checksum += value;
          }

          sendString(s);

          // checksum
          sprintf(s, "%02X\n", 0xFF - (checksum & 0xFF));
          sendString(s);

          // update terminal
          getData();
          Gui::append(buf);
        }
      }

      // skip to next line
      while(1)
      {
        temp = fgetc(fp);
        if(temp == '\n')
          break;
      }
    }
  }

  sprintf(s, "S804000000FB\n");
  sendString(s);

  fclose(fp);
}

