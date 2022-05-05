#!/bin/bash

mkdir -p /usr/x86_64-w64-mingw32/bin

# The output of `wine64 llvm-config --system-libs --cxxflags mcdisassembler`
# containes absolute path like '-IZ:'
# The sed is used to replace `-IZ:/usr/x86_64-w64-mingw32/include`
# to `-I/usr/x86_64-w64-mingw32/include`

# Debian's pkg-config wrapers for mingw are broken, and there's no sign that
# they're going to be fixed, so we'll just have to fix it ourselves
# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=930492
cat >/usr/x86_64-w64-mingw32/bin/pkg-config <<EOF
#!/bin/sh

PKG_CONFIG_LIBDIR=/usr/x86_64-w64-mingw32/lib/pkgconfig:/usr/x86_64-w64-mingw32/share/pkgconfig pkg-config \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/pkg-config

cat >/usr/x86_64-w64-mingw32/bin/llvm-config <<EOF
#!/bin/sh
wine64 llvm-config \$@ | sed -e "s,Z:/,/,gi"
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/llvm-config

cat >/usr/x86_64-w64-mingw32/bin/clang <<EOF
#!/bin/sh
wine64 clang \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/clang

cat >/usr/x86_64-w64-mingw32/bin/llvm-as <<EOF
#!/bin/sh
wine64 llvm-as \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/llvm-as

cat >/usr/x86_64-w64-mingw32/bin/llvm-link <<EOF
#!/bin/sh
wine64 llvm-link \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/llvm-link

cat >/usr/x86_64-w64-mingw32/bin/opt <<EOF
#!/bin/sh
wine64 opt \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/opt

cat >/usr/x86_64-w64-mingw32/bin/llvm-spirv <<EOF
#!/bin/sh
wine64 llvm-spirv \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/llvm-spirv
