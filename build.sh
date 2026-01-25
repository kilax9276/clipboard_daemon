# apt install -y build-essential libgtk-3-dev libglib2.0-dev
#

gcc clipboard_daemon.c -o clipboard-daemon \
  `pkg-config --cflags --libs gtk+-3.0 gio-2.0`
