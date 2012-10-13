================================================
vsjpgreader - JPEG image reader for VapourSynth
================================================

this software is based in part on the work of the Independent JPEG Group.

Usage:
------
    >>> import vapoursynth as vs
    >>> core = vs.Core()
    >>> core.std.LoadPlugin('/path/to/vsjpgreader.dll')

    - read single file:
    >>> clip = core.jpgr.Read(['/path/to/file.jpg'])

    - read two or more files:
    >>> srcs = ['/path/to/file1.jpg', '/path/to/file2.jpg', ... ,'/path/to/fileX.jpg']
    >>> clip = core.jpgr.Read(srcs)

    - read image sequence:
    >>> import os
    >>> dir = '/path/to/the/directory/'
    >>> srcs = [dir + src for src in os.listdir(dir) if src.endswith('.jpg')]
    >>> clip = core.jpgr.Read(srcs)

note:
-----
    - Generally, jpeg images are compressed after converted source RGB to 8bit planar YUV, and they are reconverted to RGB at the time of decoding.
      vsjpgreader omits the reconversion to RGB, and keep them with YUV.

    - The image width will be make into mod 4 with padding. This is not a bug but a limitation.

    - When reading two or more images, all those width, height, and chroma-subsampling-type need to be the same.

How to compile:
---------------
    Like as follows::
    
    $ git clone git://github.com/chikuzen/vsjpgreader.git
    $ cd ./vsjpgreader
    $ HERE=$(pwd)
    $ wget -O - http://sourceforge.net/projects/libjpeg-turbo/files/1.2.1/libjpeg-turbo-1.2.1.tar.gz | tar zxf -
    $ cd ./libjpeg-turbo-1.2.1
    $ autoreconf -fiv
    $ ./configure --prefix=${HERE} --disable-shared
    $ make -j4 && make test && make install-strip
    $ cd ..
    $ ./configure --extra-cflags="-I./include" --extra-ldflags="-L./lib"
    $ make

    - NASM is required for compiling libjpeg-turbo.
    - On msys/mingw, you have to install autoconf,automake and libtool into /mingw (not /usr).
      If they are not place on the expected position, autoreconf will fail.

misc:
-----
    - vsjpgreader is using TurboJPEG/OSS library. TurboJPEG/OSS is a part of libjpeg-turbo project.

    - libjpeg-turbo is a derivative of libjpeg that uses SIMD instructions (MMX, SSE2, NEON) to accelerate baseline JPEG compression and decompression on x86, x86-64, and ARM systems.

link:
-----
    vsjpgreader source code repository:

        https://github.com/chikuzen/vsjpgreader

    Independent JPEG Group:

        http://www.ijg.org/

    libjpeg-turbo:

        http://www.libjpeg-turbo.org/

Author: Oka Motofumi (chikuzen.mo at gmail dot com)
