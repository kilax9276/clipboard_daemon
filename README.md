# GTK Clipboard Daemon

## Overview

**GTK Clipboard Daemon** is a lightweight GTK-based background service that provides reliable, programmatic control over the X11 clipboard. It is designed to emulate native GNOME/GTK copy behavior for files and images, making clipboard operations initiated by scripts or external tools indistinguishable from user-driven actions.

The daemon owns both the **CLIPBOARD** and **PRIMARY** selections and keeps clipboard data alive for as long as it is running.

---

## Why this project exists

The **primary goal of this project** is to make it possible to copy files — especially **image files** — into the clipboard in such a way that they are **correctly recognized by web browsers when using Ctrl+V**.

In practice, many tools can place a file path or raw data onto the clipboard, but browsers often fail to interpret this data as a valid pasted image. This usually happens because required clipboard targets (such as `image/png` and GNOME-specific metadata) are missing or incorrectly provided.

This daemon was created to ensure that copied image files:

- Are exposed as real image data (`image/png`)
- Include proper URI and GNOME clipboard metadata
- Behave identically to images copied from native GTK/GNOME applications

As a result, images copied through this daemon can be pasted directly into browsers, chat applications, and other GUI software using **Ctrl+V**, without intermediate steps.

More generally, on X11 systems clipboard ownership is bound to the lifetime of the process that sets it, which creates additional problems for CLI tools and short-lived programs:

- Clipboard contents disappear when the process exits
- GNOME-specific targets like `x-special/gnome-copied-files` are not provided
- Image data cannot be reliably exposed
- PRIMARY and CLIPBOARD selections are often mishandled

This daemon solves these issues by acting as a persistent GTK process that owns the clipboard and serves data lazily on request.

---

## Features

- Persistent GTK clipboard owner
- Updates both **CLIPBOARD** and **PRIMARY** selections
- GNOME-compatible file copy support
- Multiple clipboard targets:
  - `x-special/gnome-copied-files`
  - `text/uri-list`
  - `image/png`
- Unix domain socket control interface
- Fully event-driven (GTK main loop)

---

## Dependencies

The project is written in C and depends on GTK 3 and GLib/GIO.

On Debian/Ubuntu-based systems, install the required packages:

```sh
apt install -y build-essential libgtk-3-dev libglib2.0-dev
```

---

## Build

Compile the daemon using `gcc` and `pkg-config`:

```sh
gcc clipboard_daemon.c -o clipboard-daemon \
  `pkg-config --cflags --libs gtk+-3.0 gio-2.0`
```

This produces a single executable:

```text
clipboard-daemon
```

No additional runtime assets or configuration files are required.

---

## Usage

### Start the daemon

Run the daemon in an X11 session:

```sh
./clipboard-daemon
```

In a Docker container, we do the following:
```sh
NO_AT_BRIDGE=1 DISPLAY=:3 ./clipboard-daemon
```


The daemon listens on the following Unix domain socket:

```text
/run/gtk-clipboard-daemon.sock
```

---

### Send clipboard commands

The control protocol is line-based and minimal.

To copy a file into the clipboard:

```sh
echo "copy /path/to/file.png" | socat - UNIX-CONNECT:/run/gtk-clipboard-daemon.sock
```
or
```sh
echo "copy /path/to/file.png" | nc -U /run/gtk-clipboard-daemon.sock
```
The ability to work with multi-line text data has been added.
```sh
c -U /run/gtk-clipboard-daemon.sock <<'EOF'
text-begin
Hello world
Line 2
Unicode: TeXt EXAMPLE
text-end
EOF
```

What happens internally:

- The file path is converted to a `file://` URI
- GNOME-compatible clipboard metadata is generated
- If readable, the file contents are exposed as `image/png`
- Both CLIPBOARD and PRIMARY selections are updated

The file can then be pasted into GTK/GNOME applications, file managers, image editors, and messengers.

---

## Use Cases

- Clipboard integration for CLI tools
- Automation and scripting
- Headless or remote-controlled environments
- Custom file managers
- Testing clipboard-dependent applications

---

## Limitations

- X11 only (not Wayland-native)
- Clipboard contents are lost when the daemon exits
- No authentication or access control on the socket
- Assumes trusted local usage

---

## Design Philosophy

- Minimal and explicit
- GTK-correct behavior over hacks
- One focused responsibility
- No unnecessary abstractions

---

## License

This project is licensed under the **Apache License, Version 2.0**.

Copyright © kilax9276 (Kolobov Aleksei)

Telegram: @kilax9276

You may obtain a copy of the License at:

```text
http://www.apache.org/licenses/LICENSE-2.0
```

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

