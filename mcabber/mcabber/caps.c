/*
 * caps.c       -- Entity Capabilities Cache for mcabber
 *
 * Copyright (C) 2008 Frank Zschockelt <mcabber@freakysoft.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <glib.h>

typedef struct {
  char *category;
  char *name;
  char *type;
  GHashTable *features;
} caps;

static GHashTable *caps_cache = NULL;

void caps_destroy(gpointer data)
{
  caps *c = data;
  g_free(c->category);
  g_free(c->name);
  g_free(c->type);
  g_hash_table_destroy(c->features);
  g_free(c);
}

void caps_init(void)
{
  if (!caps_cache)
    caps_cache = g_hash_table_new_full(g_str_hash, g_str_equal,
                                       g_free, caps_destroy);
}

void caps_free(void)
{
  if (caps_cache) {
    g_hash_table_destroy(caps_cache);
    caps_cache = NULL;
  }
}

void caps_add(char *hash)
{
  if (!hash)
    return;
  caps *c = g_new0(caps, 1);
  c->features = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  g_hash_table_insert(caps_cache, g_strdup(hash), c);
}

int caps_has_hash(const char *hash)
{
  return (hash != NULL && (g_hash_table_lookup(caps_cache, hash) != NULL));
}

void caps_set_identity(char *hash,
                       const char *category,
                       const char *name,
                       const char *type)
{
  caps *c;
  if (!hash)
    return;

  c = g_hash_table_lookup(caps_cache, hash);
  if (c) {
    c->category = g_strdup(category);
    c->name = g_strdup(name);
    c->type = g_strdup(type);
  }
}

void caps_add_feature(char *hash, const char *feature)
{
  caps *c;
  if (!hash)
    return;
  c = g_hash_table_lookup(caps_cache, hash);
  if (c) {
    char *f = g_strdup(feature);
    g_hash_table_replace(c->features, f, f);
  }
}

int caps_has_feature(char *hash, char *feature)
{
  caps *c;
  if (!hash)
    return 0;
  c = g_hash_table_lookup(caps_cache, hash);
  if (c)
    return (g_hash_table_lookup(c->features, feature) != NULL);
  return 0;
}

static GFunc _foreach_function;

void _caps_foreach_helper(gpointer key, gpointer value, gpointer user_data)
{
  // GFunc func = (GFunc)user_data;
  _foreach_function(value, user_data);
}

void caps_foreach_feature(const char *hash, GFunc func, gpointer user_data)
{
  caps *c;
  if (!hash)
    return;
  c = g_hash_table_lookup(caps_cache, hash);
  if (!c)
    return;
  _foreach_function = func;
  g_hash_table_foreach(c->features, _caps_foreach_helper, user_data);
}

gint _strcmp_sort(gconstpointer a, gconstpointer b)
{
  return g_strcmp0(a, b);
}

// Generates the sha1 hash for the special capability "" and returns it
const char *caps_generate(void)
{
  char *identity;
  GList *features;
  GChecksum *sha1;
  guint8 digest[20];
  gsize digest_size = 20;
  gchar *hash, *old_hash = NULL;
  caps *old_caps;
  caps *c = g_hash_table_lookup(caps_cache, "");

  g_hash_table_steal(caps_cache, "");
  sha1 = g_checksum_new(G_CHECKSUM_SHA1);
  identity = g_strdup_printf("%s/%s//%s<", c->category, c->type, c->name);
  g_checksum_update(sha1, (guchar*)identity, -1);
  g_free(identity);

  features = g_hash_table_get_values(c->features);
  features = g_list_sort(features, _strcmp_sort);
  {
    GList *feature;
    for (feature=features; feature; feature=feature->next) {
      g_checksum_update(sha1, feature->data, -1);
      g_checksum_update(sha1, (guchar *)"<", -1);
    }
  }
  g_list_free(features);

  g_checksum_get_digest(sha1, digest, &digest_size);
  hash = g_base64_encode(digest, digest_size);
  g_checksum_free(sha1);
  g_hash_table_lookup_extended(caps_cache, hash,
                               (gpointer *)&old_hash, (gpointer *)&old_caps);
  g_hash_table_insert(caps_cache, hash, c);
  if (old_hash)
    return old_hash;
  else
    return hash;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
