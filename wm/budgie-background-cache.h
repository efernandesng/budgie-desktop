/*
 * budgie-background-cache.h
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

#ifndef __BUDGIE_BACKGROUND_CACHE_H__
#define __BUDGIE_BACKGROUND_CACHE_H__

#include <glib.h>
#include <glib-object.h>
#include <meta/meta-background.h>

G_BEGIN_DECLS

#define BUDGIE_TYPE_BACKGROUND_CACHE budgie_background_cache_get_type()

#define BUDGIE_BACKGROUND_CACHE(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
        BUDGIE_TYPE_BACKGROUND_CACHE, BudgieBackgroundCache))

#define BUDGIE_BACKGROUND_CACHE_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST ((klass), \
        BUDGIE_TYPE_BACKGROUND_CACHE, BudgieBackgroundCacheClass))

#define BUDGIE_IS_BACKGROUND_CACHE(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
        BUDGIE_TYPE_BACKGROUND_CACHE))

#define BUDGIE_IS_BACKGROUND_CACHE_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE ((klass), \
        BUDGIE_TYPE_BACKGROUND_CACHE))

#define BUDGIE_BACKGROUND_CACHE_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj), \
        BUDGIE_TYPE_BACKGROUND_CACHE, BudgieBackgroundCacheClass))

typedef struct _BudgieBackgroundCache BudgieBackgroundCache;
typedef struct _BudgieBackgroundCacheClass BudgieBackgroundCacheClass;
typedef struct _BudgieBackgroundCachePrivate BudgieBackgroundCachePrivate;

struct _BudgieBackgroundCache
{
        GObject parent;

        BudgieBackgroundCachePrivate *priv;
};

struct _BudgieBackgroundCacheClass
{
        GObjectClass parent_class;

        /* Signal */
        void    (* file_changed) (BudgieBackgroundCache *bg_cache,
                                  const gchar *filename);
};

typedef void (*BudgieBackgroundCacheLoadImageFunc)      (ClutterContent *content,
                                                	 gpointer user_data);

GType budgie_background_cache_get_type(void) G_GNUC_CONST;

BudgieBackgroundCache *budgie_background_cache_new(void);

MetaBackground * budgie_background_cache_get_pattern_content(BudgieBackgroundCache      *self,
		                                             MetaScreen                 *screen,
		                                             guint                       monitor,
		                                             ClutterColor               *color,
		                                             ClutterColor               *second_color,
		                                             GDesktopBackgroundShading   shading_type,
		                                             MetaBackgroundEffects       effects);

void budgie_background_cache_get_image_content(BudgieBackgroundCache        *self,
		                               MetaScreen                   *screen,
		                               guint                         monitor,
		                               GDesktopBackgroundStyle       style,
		                               const gchar                  *filename,
		                               MetaBackgroundEffects         effects,
		                               GCancellable                 *cancellable,
		                               BudgieBackgroundCacheLoadImageFunc    callback,
		                               gpointer                      user_data);

void budgie_background_cache_remove_pattern_content(BudgieBackgroundCache       *self,
                                                    MetaBackground              *content);

void budgie_background_cache_remove_image_content(BudgieBackgroundCache         *self,
                                                  MetaBackground                *content);

G_END_DECLS

#endif /* __BUDGIE_BACKGROUND_CACHE_H__ */
