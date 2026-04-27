#include "halscope_app.h"

int main(int argc, char** argv) {
  g_autoptr(HalscopeApp) app = halscope_app_new();
  return g_application_run(G_APPLICATION(app), argc, argv);
}
