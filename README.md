# xdg-desktop-portal-lxqt

A backend implementation for [xdg-desktop-portal](http://github.com/flatpak/xdg-desktop-portal)
that is using Qt.

## Building xdg-desktop-portal-lxqt

### Dependencies:
- Build + Runtime
  - Qt 6

- Runtime only
  - Qt 6
  - xdg-desktop-portal

### Build instructions:
```
make
```
or
```
make YOCTO_VERSION=kirkstone
```
### Use LXQt filedialog in applications

* Firefox version 98 and higher:  Open in the address bar `about:config`, search for "portal" and set both `widget.use-xdg-desktop-portal.file-picker` and `widget.use-xdg-desktop-portal.mime-handler`  from `2` to `1`.
