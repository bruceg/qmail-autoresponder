#include "conf_bin.c"
#include "conf_man.c"
#include <installer.h>

void insthier(void) {
  int bin = opendir(conf_bin);
  int man = opendir(conf_man);
  int man1;

  c(bin,  "qmail-autoresponder",   -1, -1, 0755);

  man1 = d(man, "man1", -1, -1, 0755);
  c(man1, "qmail-autoresponder.1", -1, -1, 0644);
}
