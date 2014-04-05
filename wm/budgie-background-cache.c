/*
 * budgie-background-cache.c
 * 
 * Copyright 2014 Ikey Doherty <ikey.doherty@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA. 
 */

/* Because g_signal_handlers_disconnect_by_func */
#pragma GCC diagnostic ignored "-Wpedantic"

#include "budgie-background-cache.h"

G_DEFINE_TYPE (BudgieBackgroundCache, budgie_background_cache, G_TYPE_OBJECT)

#define BACKGROUND_CACHE_PRIVATE(o) \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), BUDGIE_TYPE_BACKGROUND_CACHE, BudgieBackgroundCachePrivate))

typedef struct {
        gboolean                        should_copy;
        gint                            monitor;
        MetaBackgroundEffects           effects;
        BudgieBackgroundCacheLoadImageFunc      callback;
        gpointer                        user_data;
} PendingFileLoadCaller;

typedef struct {
        gchar                          *filename;
        GDesktopBackgroundStyle         style;
        GList                          *callers;
} PendingFileLoad;

struct _BudgieBackgroundCachePrivate
{
        GList                          *patterns;
        GList                          *images;
        GList                          *pending_loads;

        GHashTable                     *file_monitors;
};

enum {
        FILE_CHANGED,

        LAST_SIGNAL
};

static guint obj_signals[LAST_SIGNAL] = { 0, };

static PendingFileLoadCaller * pending_file_load_caller_new(gboolean                    should_copy,
                                                            gint                        monitor,
                                                            MetaBackgroundEffects       effects,
                                                            BudgieBackgroundCacheLoadImageFunc  callback)
{
        PendingFileLoadCaller *caller = g_slice_new(PendingFileLoadCaller);

        caller->should_copy     = should_copy;
        caller->monitor         = monitor;
        caller->effects         = effects;
        caller->callback        = callback;
        caller->user_data       = NULL;

        return caller;
}

static PendingFileLoad * pending_file_load_new(const gchar              *filename,
                                               GDesktopBackgroundStyle   style)
{
        PendingFileLoad *pending_load = g_slice_new(PendingFileLoad);

        pending_load->filename  = g_strdup(filename);
        pending_load->style     = style;
        pending_load->callers   = NULL;

        return pending_load;
}

static void pending_file_load_caller_free(PendingFileLoadCaller *caller)
{
        g_slice_free(PendingFileLoadCaller, caller);
        caller = NULL;
}

static void pending_file_load_free(PendingFileLoad *pending_load)
{
        g_free(pending_load->filename);
        g_list_free_full(pending_load->callers,
                         (GDestroyNotify) pending_file_load_caller_free);
        g_slice_free(PendingFileLoad, pending_load);
        pending_load = NULL;
}

static void remove_list_content(GList *list, MetaBackground *content)
{
        GList *list_node;

        list_node = g_list_first(list);
        while (list_node != NULL) {
                GList *link;
                MetaBackground *node_content = (MetaBackground *) list_node->data;

                if (node_content == content) {
                        link = list_node;
                        list_node = g_list_next(list_node);
                        list = g_list_delete_link(list, link);
                        g_object_unref(node_content);
                } else {
                        list_node = g_list_next(list_node);
                }
        }
}

static void file_changed_cb(GFileMonitor     *monitor,
                            GFile            *file,
                            GFile            *other_file,
                            GFileMonitorEvent event_type,
                            gpointer          user_data)
{
        BudgieBackgroundCache *bg_cache = BUDGIE_BACKGROUND_CACHE(user_data);
        MetaBackground *content = NULL;
        gchar *filename = NULL;
        GList *list_node;

        filename = g_file_get_path(file);
        list_node = g_list_first(bg_cache->priv->images);
        while (list_node != NULL) {
                GList *link;
                content = (MetaBackground *) list_node->data;

                if (g_strcmp0(meta_background_get_filename(content), filename) == 0) {
                        link = list_node;
                        list_node = g_list_next(list_node);
                        bg_cache->priv->images = g_list_delete_link(bg_cache->priv->images,
                                                                    link);
                        g_object_unref(content);
                } else {
                        list_node = g_list_next(list_node);
                }
        }

        /* Disconnect signal */
        g_signal_handlers_disconnect_by_func(monitor,
                                             G_CALLBACK(file_changed_cb),
                                             bg_cache);

        g_signal_emit(bg_cache, obj_signals[FILE_CHANGED], 0, filename);

        g_free(filename);
}

static void background_load_file_cb(GObject            *source_object,
                                    GAsyncResult       *res,
                                    gpointer            user_data)
{
        BudgieBackgroundCache *bg_cache = BUDGIE_BACKGROUND_CACHE(user_data);
        MetaBackground *content = META_BACKGROUND(source_object);
        const gchar *content_filename;
        GDesktopBackgroundStyle content_style;
        GFile *image_file;
        GFileMonitor *file_monitor = NULL;
        GList *node_pend_load, *node_caller;
        gboolean result;
        GError *error = NULL;

        result = meta_background_load_file_finish(content,
                                                  res,
                                                  &error);
        
        content_filename = meta_background_get_filename(content);
        content_style = meta_background_get_style(content);
        
        if (!result) {
                g_warning("%s", error->message);
                g_error_free(error);

                //g_object_unref(content);
                content = NULL;
        } else {
                /* Monitor file */
                image_file = g_file_new_for_path(content_filename);
                file_monitor = g_file_monitor(image_file,
                                              G_FILE_MONITOR_NONE,
                                              NULL,
                                              &error);
                if (file_monitor == NULL) {
                        g_warning("%s", error->message);
                        g_error_free(error);

                        g_object_unref(content);
                        content = NULL;
                } else {
                        g_signal_connect(file_monitor,
                                         "changed",
                                         G_CALLBACK(file_changed_cb),
                                         bg_cache);
                        g_hash_table_insert(bg_cache->priv->file_monitors,
                                            (gpointer) g_strdup(content_filename),
                                            file_monitor);
                }
        }

        for (node_pend_load = g_list_first(bg_cache->priv->pending_loads);
             node_pend_load != NULL;
             node_pend_load = g_list_next(node_pend_load))
        {
                PendingFileLoad *pending_load = (PendingFileLoad *) node_pend_load->data;

                if (g_strcmp0(pending_load->filename, content_filename) ||
                    pending_load->style != content_style)
                        continue;

                /* Call pending loads callbacks */
                for (node_caller = g_list_first(pending_load->callers);
                     node_caller != NULL;
                     node_caller = g_list_next(node_caller))
                {
                        PendingFileLoadCaller *caller = (PendingFileLoadCaller *) node_caller->data;

                        if (caller->callback != NULL) {
                                ClutterContent *new_content = NULL;

                                if (content != NULL && caller->should_copy) {
                                        new_content = CLUTTER_CONTENT(meta_background_copy(content,
                                                                                           caller->monitor,
                                                                                           caller->effects));
                                        bg_cache->priv->images = g_list_append(bg_cache->priv->images,
                                                                               new_content);
                                }

                                caller->callback(new_content, caller->user_data);
                        }
                }
        }
}

static void budgie_background_cache_load_image_content(BudgieBackgroundCache        *self,
                                                       MetaScreen                   *screen,
                                                       guint                         monitor,
                                                       GDesktopBackgroundStyle       style,
                                                       const gchar                  *filename,
                                                       MetaBackgroundEffects         effects,
                                                       GCancellable                 *cancellable,
                                                       BudgieBackgroundCacheLoadImageFunc    callback,
                                                       gpointer                      user_data)
{
        MetaBackground *content = NULL;
        PendingFileLoad *pending_load = NULL;
        PendingFileLoadCaller *caller = NULL;
        GList *list_node;

        for (list_node = g_list_first(self->priv->pending_loads);
             list_node != NULL;
             list_node = g_list_next(self->priv->pending_loads))
        {
                pending_load = (PendingFileLoad *) list_node->data;

                if (g_strcmp0(pending_load->filename, filename) == 0 &&
                    pending_load->style == style) {

                        caller = pending_file_load_caller_new(TRUE,
                                                              monitor,
                                                              effects,
                                                              callback);

                        pending_load->callers = g_list_append(pending_load->callers,
                                                              caller);
                        return;
                }
        }

        pending_load = pending_file_load_new(filename, style);
        caller = pending_file_load_caller_new(FALSE,
                                              monitor,
                                              effects,
                                              callback);
        pending_load->callers = g_list_append(pending_load->callers,
                                              caller);

        content = meta_background_new(screen, monitor, effects);

        meta_background_load_file_async(content,
                                        filename,
                                        style,
                                        cancellable,
                                        background_load_file_cb,
                                        self);
}

static void budgie_background_cache_dispose(GObject *object)
{
        BudgieBackgroundCachePrivate *priv = BUDGIE_BACKGROUND_CACHE(object)->priv;

        /* Patterns */
        if (priv->patterns != NULL) {
                g_list_free_full(priv->patterns, g_object_unref);
                priv->patterns = NULL;
        }

        /* Images */
        if (priv->images != NULL) {
                g_list_free_full(priv->images, g_object_unref);
                priv->images = NULL;
        }

        /* Pending loads */
        if (priv->pending_loads != NULL) {
                g_list_free_full(priv->pending_loads,
                                 (GDestroyNotify) pending_file_load_free);
                priv->pending_loads = NULL;
        }

        /* File monitors */
        if (priv->file_monitors != NULL) {
                g_hash_table_remove_all(priv->file_monitors);
                g_hash_table_destroy(priv->file_monitors);
                priv->file_monitors = NULL;
        }

        G_OBJECT_CLASS (budgie_background_cache_parent_class)->dispose (object);
}

static void budgie_background_cache_finalize(GObject *object)
{
        G_OBJECT_CLASS (budgie_background_cache_parent_class)->finalize (object);
}

static void budgie_background_cache_class_init(BudgieBackgroundCacheClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (BudgieBackgroundCachePrivate));

        object_class->dispose = budgie_background_cache_dispose;
        object_class->finalize = budgie_background_cache_finalize;

        obj_signals[FILE_CHANGED]       = g_signal_new("file-changed",
                                                       G_TYPE_FROM_CLASS(klass),
                                                       G_SIGNAL_RUN_FIRST,
                                                       G_STRUCT_OFFSET(BudgieBackgroundCacheClass, file_changed),
                                                       NULL, NULL,
                                                       g_cclosure_marshal_VOID__STRING, /* TODO gen marshallist */
                                                       G_TYPE_NONE,
                                                       1,
                                                       G_TYPE_STRING);
}

static void budgie_background_cache_init(BudgieBackgroundCache *self)
{
        BudgieBackgroundCachePrivate *priv;

        self->priv = priv = BACKGROUND_CACHE_PRIVATE (self);

        priv->patterns = NULL;
        priv->images = NULL;
        priv->pending_loads = NULL;

        priv->file_monitors = g_hash_table_new_full(g_str_hash,
                                                    g_str_equal,
                                                    (GDestroyNotify) g_free,          /* key */
                                                    (GDestroyNotify) g_object_unref); /* value */
}


BudgieBackgroundCache * budgie_background_cache_new(void)
{
        return g_object_new (BUDGIE_TYPE_BACKGROUND_CACHE, NULL);
}

MetaBackground * budgie_background_cache_get_pattern_content(BudgieBackgroundCache      *self,
                                                             MetaScreen                 *screen,
                                                             guint                       monitor,
                                                             ClutterColor               *color,
                                                             ClutterColor               *second_color,
                                                             GDesktopBackgroundShading   shading_type,
                                                             MetaBackgroundEffects       effects)
{
        g_return_val_if_fail(BUDGIE_IS_BACKGROUND_CACHE(self), NULL);

        MetaBackground *content = NULL;
        MetaBackground *candidate_content = NULL;
        MetaBackgroundEffects content_effects;
        GList *list_node;

        for (list_node = g_list_first(self->priv->patterns);
             list_node != NULL;
             list_node = g_list_next(list_node))
        {
                content = (MetaBackground *) list_node->data;

                if (meta_background_get_shading(content) != shading_type)
                        continue;

                if (!clutter_color_equal(meta_background_get_color(content),
                                         color))
                        continue;

                if (shading_type != G_DESKTOP_BACKGROUND_SHADING_SOLID &&
                    !clutter_color_equal(meta_background_get_second_color(content),
                                            second_color))
                {
                        continue;
                }

                candidate_content = content;

                g_object_get(content, "effects", &content_effects, NULL);
                if (effects != content_effects)
                        continue;

                break;
        }

        if (candidate_content == NULL) {
                content = meta_background_new(screen,
                                              monitor,
                                              effects);

                if (shading_type == G_DESKTOP_BACKGROUND_SHADING_SOLID) {
                        meta_background_load_color(content, color);
                } else {
                        meta_background_load_gradient(content,
                                                      shading_type,
                                                      color,
                                                      second_color);
                }

                self->priv->patterns = g_list_append(self->priv->patterns,
                                                     content);
        } else {
                content = candidate_content;
                g_object_set(content,
                             "monitor", monitor,
                             "effects", effects,
                             NULL);
        }

        return content;
}

void budgie_background_cache_get_image_content(BudgieBackgroundCache        *self,
                                               MetaScreen                   *screen,
                                               guint                         monitor,
                                               GDesktopBackgroundStyle       style,
                                               const gchar                  *filename,
                                               MetaBackgroundEffects         effects,
                                               GCancellable                 *cancellable,
                                               BudgieBackgroundCacheLoadImageFunc    callback,
                                               gpointer                      user_data)
{
        g_return_if_fail(BUDGIE_IS_BACKGROUND_CACHE(self));

        MetaBackground *content = NULL;
        MetaBackground *candidate_content = NULL;
        GDesktopBackgroundStyle content_style;
        MetaBackgroundEffects content_effects;
        gint content_monitor;
        GList *list_node;

        for (list_node = g_list_first(self->priv->images);
             list_node != NULL;
             list_node = g_list_next(list_node))
        {
                content = (MetaBackground *) list_node->data;

                content_style = meta_background_get_style(content);
                if (content_style != style)
                        continue;

                if (g_strcmp0(meta_background_get_filename(content), filename))
                        continue;

                g_object_get(content, "monitor", &content_monitor, NULL);
                if (content_style == G_DESKTOP_BACKGROUND_STYLE_SPANNED &&
                    content_monitor != monitor)
                {
                        continue;
                }

                candidate_content = content;

                g_object_get(content, "effects", &content_effects, NULL);
                if (content_effects != effects)
                        continue;

                break;
        }

        if (candidate_content == NULL) {
                budgie_background_cache_load_image_content(self,
                                                           screen,
                                                           monitor,
                                                           style,
                                                           filename,
                                                           effects,
                                                           cancellable,
                                                           callback,
                                                           user_data);
        } else {
                content = candidate_content;
                g_object_set(content,
                             "monitor", monitor,
                             "effects", effects,
                             NULL);

                if (cancellable != NULL && g_cancellable_is_cancelled(cancellable))
                        content = NULL;
                else
                        callback(CLUTTER_CONTENT(content), user_data);
        }
}

void budgie_background_cache_remove_pattern_content(BudgieBackgroundCache       *self,
                                                    MetaBackground              *content)
{
        g_return_if_fail(BUDGIE_IS_BACKGROUND_CACHE(self) && META_IS_BACKGROUND(content));

        remove_list_content(self->priv->patterns, content);
}

void budgie_background_cache_remove_image_content(BudgieBackgroundCache         *self,
                                                  MetaBackground                *content)
{
        g_return_if_fail(BUDGIE_IS_BACKGROUND_CACHE(self) && META_IS_BACKGROUND(content));

        GList *list_node;
        gboolean has_others = FALSE;
        const gchar *filename = meta_background_get_filename(content);

        list_node = g_list_first(self->priv->images);
        while (list_node != NULL && !has_others) {
                MetaBackground *temp = (MetaBackground *) list_node->data;

                if (g_strcmp0(meta_background_get_filename(temp), filename) == 0) {
                        has_others = TRUE;
                }
        }

        if (!has_others)
                g_hash_table_remove(self->priv->file_monitors, filename);

        remove_list_content(self->priv->images, content);
}
