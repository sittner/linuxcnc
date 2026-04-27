#ifndef HALSCOPE_APP_H_
#define HALSCOPE_APP_H_

#include <gtk/gtk.h>

G_DECLARE_FINAL_TYPE(HalscopeApp, halscope_app, HALSCOPE, APP,
                     GtkApplication)

HalscopeApp* halscope_app_new();

#endif  // HALSCOPE_APP_H_
