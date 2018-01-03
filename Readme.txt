  HTTPPONG
  --------

  Copyright (c) 2008, 2009 by Wincent Balin
  
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
      * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * The name of Wincent Balin may not be used to endorse or promote
        products derived from this software without specific prior written
        permission.

  THIS SOFTWARE IS PROVIDED BY Wincent Balin ''AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL Wincent Balin BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  
  This is a very small HTTP server running on a 8051 controller.
  The memory (IRAM) usage is 76 bytes, so the program is able to run
  without externam RAM. ROM usage is of 2950 bytes, so any 8051 compatible
  controller with ROM of 4kb or more may be used. Only the peripherals
  every 8051 compatible controller has were used, so the resulting
  hex-file should be binary compatible across the whole gross of the
  8051 family.
  
  Of the four banks of the 8051 banks 0 and 1 are used; bank 0 is used
  for the main program and bank 1 is used for serial communication.
  Two bytes in the bit memory are used for, well, the bit memory.
  
  The inspiration and partly a source for this program was found
  in the phpstack, PHP TCP/IP stack by Adam Dunkels, and in the miniweb,
  the minimal HTTP server, by Adam Dunkels too. The idea how to overcome
  SLIP handshake by WinXX OS was found in the application note AN-32
  for the Pumpkin RTOS. Initial setup of the UART was taken
  from the book "Mikrocomputertechnik" by Bernd-Dieter Schaaf.
  
  The name of the program comes from using a xxxpong.c names for
  different stages of TCP/ICMP/IP/SLIP/RS-232 processing, i.e. to see
  that the controller sends back an answer, a "pong", to every
  TCP connection, the stage was named "tcppong.c". So "httppong"
  is the name for the last stage of development.

  ICMP protocol (i.e. ping) requests are supported.
  
  The HTTP server is stateless, and the TCP/IP stack does not support
  packet fragmentation. That means, that the HTTP request has to fit
  into one packet; same goes for the HTTP answer and consequently
  for the WWW page which server sends to the HTTP client.
  Another peculiarity, attributed both to the need of low memory usage
  and to the statelessness of the HTTP server is that the data to be sent
  over HTTP is read twice, first time for checksum calculation and
  second time for actual transfer.

  The function names in the source are pretty self-explanatory.
  For example, ip_rx2() means "receive 2 bytes using IP protocol".
  
  The source code was compiled using SDCC 2.8.0 compiler. The result
  of the compilation was the file httppong.ihx . Using packihx utility
  from the FLIP 3.x programming software was used to rewrite contents
  of this file into the file httppong.hex , which can be flashed directly
  into the ROM of any 8051 derivate.

  The UART of this web server is configured at 2400 baud, 8N1.

