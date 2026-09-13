extractrc ../src/kcm/*.ui >/tmp/rc.cpp

xgettext --from-code=UTF-8 -C -kde \
  -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 \
  -x /dev/null \
  -o kwin_better_blur_dx.pot \
  /tmp/rc.cpp ../src/kcm/*.cpp
