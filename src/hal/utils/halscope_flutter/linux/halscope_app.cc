#include "halscope_app.h"

#include <flutter_linux/flutter_linux.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <unistd.h>
#include <linux/limits.h>

#include "flutter/generated_plugin_registrant.h"

// Resolve the directory containing this executable.
static gchar* get_exe_dir() {
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len < 0) return g_strdup(".");
  buf[len] = '\0';
  gchar* dir = g_path_get_dirname(buf);
  return dir;
}

struct _HalscopeApp {
  GtkApplication parent_instance;
  char** dart_entrypoint_arguments;
  FlView* view;
};

G_DEFINE_TYPE(HalscopeApp, halscope_app, GTK_TYPE_APPLICATION)

static void halscope_app_activate(GApplication* application) {
  HalscopeApp* self = HALSCOPE_APP(application);

  // Avoid re-creating the window if already activated (e.g. from command_line).
  GtkWindow* window = gtk_application_get_active_window(GTK_APPLICATION(application));
  if (window != NULL) {
    gtk_window_present(window);
    return;
  }

  window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));

  gboolean use_header_bar = TRUE;
#ifdef GDK_WINDOWING_X11
  GdkScreen* screen = gtk_window_get_screen(window);
  if (GDK_IS_X11_SCREEN(screen)) {
    const gchar* wm_name = gdk_x11_screen_get_window_manager_name(screen);
    if (g_strcmp0(wm_name, "GNOME Shell") != 0) {
      use_header_bar = FALSE;
    }
  }
#endif
  if (use_header_bar) {
    GtkHeaderBar* header_bar = GTK_HEADER_BAR(gtk_header_bar_new());
    gtk_widget_show(GTK_WIDGET(header_bar));
    gtk_header_bar_set_title(header_bar, "LinuxCNC Halscope");
    gtk_header_bar_set_show_close_button(header_bar, TRUE);
    gtk_window_set_titlebar(window, GTK_WIDGET(header_bar));
  } else {
    gtk_window_set_title(window, "LinuxCNC Halscope");
  }

  gtk_window_set_default_size(window, 1024, 700);
  gtk_widget_show(GTK_WIDGET(window));

  g_autoptr(FlDartProject) project = fl_dart_project_new();
  fl_dart_project_set_dart_entrypoint_arguments(
      project, self->dart_entrypoint_arguments);

  // Resolve paths relative to executable:
  //   bin/halscope              (this binary)
  //   lib/flutter/              (shared: engine, icudtl.dat)
  //   lib/flutter/halscope/     (per-app: libapp.so, flutter_assets/)
  g_autofree gchar* exe_dir = get_exe_dir();
  g_autofree gchar* flutter_dir = g_build_filename(exe_dir, "..", "lib", "flutter", NULL);
  g_autofree gchar* app_dir = g_build_filename(flutter_dir, "halscope", NULL);

  g_autofree gchar* aot_path = g_build_filename(app_dir, "libapp.so", NULL);
  g_autofree gchar* assets_path = g_build_filename(app_dir, "flutter_assets", NULL);
  g_autofree gchar* icu_path = g_build_filename(flutter_dir, "icudtl.dat", NULL);

  fl_dart_project_set_aot_library_path(project, aot_path);
  fl_dart_project_set_assets_path(project, assets_path);
  fl_dart_project_set_icu_data_path(project, icu_path);

  self->view = fl_view_new(project);
  gtk_widget_show(GTK_WIDGET(self->view));
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(self->view));

  fl_register_plugins(FL_PLUGIN_REGISTRY(self->view));

  gtk_widget_grab_focus(GTK_WIDGET(self->view));
}

static gint halscope_app_command_line(GApplication* application,
                                      GApplicationCommandLine* command_line) {
  HalscopeApp* self = HALSCOPE_APP(application);
  gchar** arguments =
      g_application_command_line_get_arguments(command_line, NULL);
  self->dart_entrypoint_arguments = g_strdupv(arguments + 1);

  g_strfreev(arguments);

  g_application_activate(application);

  return 0;
}

static void halscope_app_dispose(GObject* object) {
  HalscopeApp* self = HALSCOPE_APP(object);
  g_clear_pointer(&self->dart_entrypoint_arguments, g_strfreev);
  g_clear_object(&self->view);
  G_OBJECT_CLASS(halscope_app_parent_class)->dispose(object);
}

static void halscope_app_class_init(HalscopeAppClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = halscope_app_dispose;
  G_APPLICATION_CLASS(klass)->activate = halscope_app_activate;
  G_APPLICATION_CLASS(klass)->command_line = halscope_app_command_line;
}

static void halscope_app_init(HalscopeApp* self) {}

HalscopeApp* halscope_app_new() {
  return HALSCOPE_APP(g_object_new(
      halscope_app_get_type(), "application-id", "org.linuxcnc.halscope",
      "flags",
      G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_NON_UNIQUE, NULL));
}
