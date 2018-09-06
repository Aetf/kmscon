# KMSCON 

Kmscon is a simple terminal emulator based on linux kernel mode setting (KMS).
It is an attempt to replace the in-kernel VT implementation with a userspace
console. See kmscon(1) man-page for usage information.

Website:
  https://github.com/Aetf/kmscon

## Requirements 

  Kmscon requires the following software:
    - libtsm: terminal emulator state machine
        https://github.com/Aetf/libtsm
    - libudev: providing input, video, etc. device hotplug support (>=v172)
        http://www.freedesktop.org/wiki/Software/systemd/
    - libxkbcommon: providing internationalized keyboard handling
        http://xkbcommon.org/
    - libdrm: graphics access to DRM/KMS subsystem
    - linux-headers: linux kernel headers for ABI definitions

  Everything else is optional:

    For video output at least one of the following is required:
      - fbdev: For framebuffer video output the kernel headers must be installed
               and located in the default include path.
      - DRM: For unaccelerated drm output the "libdrm" library must be installed
             and accessible via pkg-config.
      - OpenGLES2: For accelerated video output via OpenGLESv2 the following must
                   be installed: libdrm, libgbm, egl, glesv2 (i.e., mesa)

    For font handling the following is required:
      - 8x16: The 8x16 font is a static built-in font which does not require
              external dependencies.
      - unifont: Static font without external dependencies.
      - pango: drawing text with pango
               Pango requires: glib, pango, fontconfig, freetype2 and more

    For multi-seat support you need the following packages:
      - systemd: Actually only the systemd-logind daemon and library is required.

## Download 

Source can be found at:

  https://github.com/Aetf/kmscon

## Install 

- Ubuntu Server 18.04

```
$ apt install -y libudev-dev libxkbcommon-dev libdrm-dev linux-headers-$(uname -r)
$ cd ~ && git clone https://github.com/Aetf/libtsm && cd libstm
$ test -f ./configure || NOCONFIGURE=1 ./autogen.sh
$ ./configure
$ make
$ make install
$ cd ~ && git clone https://github.com/Aetf/kmscon && cd kmscon
$ test -f ./configure || NOCONFIGURE=1 ./autogen.sh
$ ./configure
$ make
$ make install
```

  If you want only a very basic kmscon program without any major dependencies,
  use:
    $ ./configure --with-video=fbdev,drm2d --with-renderers= --with-fonts=unifont --disable-multi-seat --with-sessions=dummy,terminal
  However, you will loose a lot of functionality by dropping all dependencies.

  The following configure options are available. If build-time dependencies
  cannot be satisfied, an option is automatically turned off, except if you
  explicitly enable it via command line:
    --enable-multi-seat: This requires the systemd-logind library to provide
                         multi-seat support for kmscon. [default: on]
    --enable-debug: Enable debug mode/messages [default: on]
    --enable-optimizations: Enable code optimizations [default: on]

  Backends for several subsystems in kmscon can be selected with the following
  options (all of them take a comma-separated list of backend names):
    --with-video: Video backens. Available backends are:
       - fbdev: Linux fbdev video backend
       - drm2d: Linux DRM software-rendering backend
       - drm3d: Linux DRM hardware-rendering backend
       Default is: fbdev,drm2d,drm3d
    --with-fonts: Font renderers. Available backends are:
       - unifont: Static built-in non-scalable font (Unicode Unifont)
       - pango: Pango based scalable font renderer
       Default is: unifont,pango
       The 8x16 backend is always built-in.
    --with-renderers: Console rendering backends. Available are:
       - bbulk: Simple 2D software-renderer (bulk-mode)
       - gltex: OpenGLESv2 accelerated renderer
       - pixman: pixman based renderer
       Default is: bbulk,gltex
       The bblit backend is always built-in.
    --with-sessions: Built in sessions. Available sessions are:
       - dummy: Dummy fallback session
       - terminal: Terminal-emulator sessions

## Running

  ** kmscon must be run as root **

  ```
  $ sudo LD_LIBRARY_PATH=/usr/local/lib
  $ sudo export LD_LIBRARY_PATH
  $ sudo kmscon
  ```

  To get usage information, run:
  ```
  $ ./kmscon --help
  ```

  You can then run kmscon with:
  ```
  $ ./kmscon [options]
  ```

  For debug output use "--debug". For verbose output use "--verbose".
  With "--xkb-layout=<lang>" you can switch the keyboard layout.
  See "man kmscon" / kmscon(1) for more information.

## License

  This software is licensed under the terms of an MIT-like license. Please see
  ./COPYING for further information.

## FAQ

  Please see: 

https://github.com/Aetf/libtsm

https://dvdhrm.wordpress.com/2012/08/11/kmscon-linux-kmsdrm-based-virtual-console/

## Contact

  This software is maintained by:
    David Herrmann <dh.herrmann@gmail.com>
  If you have any questions, do not hesitate to contact one of the maintainers.
