/*
 * caps.c       -- Entity Capabilities Cache for mcabber
 *
 * Copyright (C) 2008-2010 Frank Zschockelt <mcabber@freakysoft.de>
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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "settings.h"
#include "utils.h"

typedef struct {
  char *category;
  char *type;
  char *name;
} identity;

typedef struct {
  GHashTable *fields;
} dataform;

typedef struct {
  GHashTable *identities;
  GHashTable *features;
  GHashTable *forms;
} caps;

static GHashTable *caps_cache = NULL;

void caps_destroy(gpointer data)
{
  caps *c = data;
  g_hash_table_destroy(c->identities);
  g_hash_table_destroy(c->features);
  g_hash_table_destroy(c->forms);
  g_free(c);
}

void identity_destroy(gpointer data)
{
  identity *i = data;
  g_free(i->category);
  g_free(i->type);
  g_free(i->name);
  g_free(i);
}

void form_destroy(gpointer data)
{
  dataform *f = data;
  g_hash_table_destroy(f->fields);
  g_free(f);
}

void field_destroy(gpointer data)
{
  GList *v = data;
  g_list_foreach (v, (GFunc) g_free, NULL);
  g_list_free (v);
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

void caps_add(const char *hash)
{
  if (!hash)
    return;
  caps *c = g_new0(caps, 1);
  c->features = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  c->identities = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, identity_destroy);
  c->forms = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, form_destroy);
  g_hash_table_replace(caps_cache, g_strdup(hash), c);
}

void caps_remove(const char *hash)
{
  if (!hash)
    return;
  g_hash_table_remove(caps_cache, hash);
}

/* if hash is not verified, this will bind capabilities set only with bare jid */
void caps_move_to_local(char *hash, char *bjid)
{
  char *orig_hash;
  caps *c = NULL;
  if (!hash || !bjid)
    return;
  g_hash_table_lookup_extended(caps_cache, hash, (gpointer*)&orig_hash, (gpointer*)&c);
  if (c) {
    g_hash_table_steal(caps_cache, hash);
    g_free(orig_hash);
    g_hash_table_replace(caps_cache, g_strdup_printf("%s/#%s", bjid, hash), c);
    // solidus is guaranteed to never appear in bare jid
    // hash will not appear in base64 encoded hash
    // sequence "/#" is deterministic separator, and allows to identify local cache entry
  }
}

/*if bjid is NULL, it will check only verified hashes */
int caps_has_hash(const char *hash, const char *bjid)
{
  caps *c = NULL;
  if (!hash)
    return 0;
  c = g_hash_table_lookup(caps_cache, hash);
  if (!c && bjid) {
    char *key = g_strdup_printf("%s/#%s", bjid, hash);
    c = g_hash_table_lookup(caps_cache, key);
    g_free(key);
  }
  return (c != NULL);
}

void caps_add_identity(const char *hash,
                       const char *category,
                       const char *name,
                       const char *type,
                       const char *lang)
{
  caps *c;
  if (!hash || !category || !type)
    return;
  if (!lang)
    lang = "";

  c = g_hash_table_lookup(caps_cache, hash);
  if (c) {
    identity *i = g_new0(identity, 1);

    i->category = g_strdup(category);
    i->name = g_strdup(name);
    i->type = g_strdup(type);
    g_hash_table_replace(c->identities, g_strdup(lang), i);
  }
}

void caps_set_identity(char *hash,
                       const char *category,
                       const char *name,
                       const char *type)
{
  caps_add_identity(hash, category, name, type, NULL);
}

void caps_add_dataform(const char *hash, const char *formtype)
{
  caps *c;
  if (!formtype)
    return;
  c = g_hash_table_lookup(caps_cache, hash);
  if (c) {
    dataform *d = g_new0(dataform, 1);
    char *f = g_strdup(formtype);

    d->fields = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, field_destroy);
    g_hash_table_replace(c->forms, f, d);
  }
}

gint _strcmp_sort(gconstpointer a, gconstpointer b)
{
  return g_strcmp0(a, b);
}

void caps_add_dataform_field(const char *hash, const char *formtype,
                             const char *field, const char *value)
{
  caps *c;
  if (!formtype || !field || !value)
    return;
  c = g_hash_table_lookup(caps_cache, hash);
  if (c) {
    dataform *d;
    d = g_hash_table_lookup(c->forms, formtype);
    if (d) {
      gpointer key, val;
      char *f;
      GList *v = NULL;
      if (g_hash_table_lookup_extended(d->fields, field, &key, &val)) {
        g_hash_table_steal(d->fields, field);
        g_free(key);
        v = val;
      }
      f = g_strdup(field);
      v = g_list_insert_sorted(v, g_strdup(value), _strcmp_sort);
      g_hash_table_replace(d->fields, f, v);
    }
  }
}

void caps_add_feature(const char *hash, const char *feature)
{
  caps *c;
  if (!hash || !feature)
    return;
  c = g_hash_table_lookup(caps_cache, hash);
  if (c) {
    char *f = g_strdup(feature);
    g_hash_table_replace(c->features, f, f);
  }
}

/* If hash is verified, then bare jid is ignored.
 * If there is no globally verified hash, and bare jid is not null,
 * then local storage for that jid will be checked */
int caps_has_feature(const char *hash, char *feature, char *bjid)
{
  caps *c = NULL;
  if (!hash || !feature)
    return 0;
  c = g_hash_table_lookup(caps_cache, hash);
  if (!c && bjid) {
    char *key = g_strdup_printf("%s/#%s", bjid, hash);
    c = g_hash_table_lookup(caps_cache, key);
    g_free(key);
  }
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

// Generates the sha1 hash for the special capability "" and returns it
const char *caps_generate(void)
{
  GList *features, *langs;
  GChecksum *sha1;
  guint8 digest[20];
  gsize digest_size = 20;
  gchar *hash, *old_hash = NULL;
  caps *old_caps;
  caps *c = g_hash_table_lookup(caps_cache, "");

  g_hash_table_steal(caps_cache, "");
  sha1 = g_checksum_new(G_CHECKSUM_SHA1);

  langs = g_hash_table_get_keys(c->identities);
  langs = g_list_sort(langs, _strcmp_sort);
  {
    identity *i;
    GList *lang;
    char *identity_S;
    for (lang=langs; lang; lang=lang->next) {
      i = g_hash_table_lookup(c->identities, lang->data);
      identity_S = g_strdup_printf("%s/%s/%s/%s<", i->category, i->type,
                                   (char *)lang->data, i->name ? i->name : "");
      g_checksum_update(sha1, (guchar *)identity_S, -1);
      g_free(identity_S);
    }
  }
  g_list_free(langs);

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

gboolean caps_verify(const char *hash, char *function)
{
  GList *features, *langs, *forms;
  GChecksum *checksum;
  guint8 digest[20];
  gsize digest_size = 20;
  gchar *local_hash;
  gboolean match = FALSE;
  caps *c = g_hash_table_lookup(caps_cache, hash);

  if (!g_strcmp0(function, "sha-1")) {
    checksum = g_checksum_new(G_CHECKSUM_SHA1);
  } else if (!g_strcmp0(function, "md5")) {
    checksum = g_checksum_new(G_CHECKSUM_MD5);
    digest_size = 16;
  } else
    return FALSE;

  langs = g_hash_table_get_keys(c->identities);
  langs = g_list_sort(langs, _strcmp_sort);
  {
    identity *i;
    GList *lang;
    char *identity_S;
    for (lang=langs; lang; lang=lang->next) {
      i = g_hash_table_lookup(c->identities, lang->data);
      identity_S = g_strdup_printf("%s/%s/%s/%s<", i->category, i->type,
                                   (char *)lang->data, i->name ? i->name : "");
      g_checksum_update(checksum, (guchar *)identity_S, -1);
      g_free(identity_S);
    }
  }
  g_list_free(langs);

  features = g_hash_table_get_values(c->features);
  features = g_list_sort(features, _strcmp_sort);
  {
    GList *feature;
    for (feature=features; feature; feature=feature->next) {
      g_checksum_update(checksum, feature->data, -1);
      g_checksum_update(checksum, (guchar *)"<", -1);
    }
  }
  g_list_free(features);

  forms = g_hash_table_get_keys(c->forms);
  forms = g_list_sort(forms, _strcmp_sort);
  {
    dataform *d;
    GList *form, *fields;
    for (form=forms; form; form=form->next) {
      d = g_hash_table_lookup(c->forms, form->data);
      g_checksum_update(checksum, form->data, -1);
      g_checksum_update(checksum, (guchar *)"<", -1);
      fields = g_hash_table_get_keys(d->fields);
      fields = g_list_sort(fields, _strcmp_sort);
      {
        GList *field;
        GList *values;
        for (field=fields; field; field=field->next) {
          g_checksum_update(checksum, field->data, -1);
          g_checksum_update(checksum, (guchar *)"<", -1);
          values = g_hash_table_lookup(d->fields, field->data);
          {
            GList *value;
            for (value=values; value; value=value->next) {
              g_checksum_update(checksum, value->data, -1);
              g_checksum_update(checksum, (guchar *)"<", -1);
            }
          }
        }
      }
      g_list_free(fields);
    }
  }
  g_list_free(forms);

  g_checksum_get_digest(checksum, digest, &digest_size);
  local_hash = g_base64_encode(digest, digest_size);
  g_checksum_free(checksum);

  match = !g_strcmp0(hash, local_hash);

  g_free(local_hash);
  return match;
}

static gchar* caps_get_filename(const char* hash)
{
  gchar *hash_fs;
  gchar *dir = (gchar *) settings_opt_get ("caps_directory");
  gchar *file = NULL;

  if (!dir)
    goto caps_filename_return;

  hash_fs = g_strdup (hash);
  {
    const gchar *valid_fs =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+=";
    g_strcanon(hash_fs, valid_fs, '-');
  }

  dir = expand_filename (dir);
  file = g_strdup_printf ("%s/%s.ini", dir, hash_fs);
  g_free(dir);
  g_free(hash_fs);

caps_filename_return:
  return file;
}

/* Store capabilities set in GKeyFile. To be used with verified hashes only */
void caps_copy_to_persistent(const char* hash, char* xml)
{
  gchar *file;
  GList *features, *langs, *forms;
  GKeyFile *key_file;
  caps *c;
  int fd;

  g_free (xml);

  c = g_hash_table_lookup (caps_cache, hash);
  if (!c)
    goto caps_copy_return;

  file = caps_get_filename (hash);
  if (!file)
    goto caps_copy_return;

  fd = open (file, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
  if (fd == -1)
    goto caps_copy_exists;

  key_file = g_key_file_new ();
  g_key_file_set_comment (key_file, NULL, NULL,
                          "This is autogenerated file. Please do not modify.",
                          NULL);

  langs = g_hash_table_get_keys (c->identities);
  {
    identity *i;
    GList *lang;
    gchar *group;
    for (lang=langs; lang; lang=lang->next) {
      i = g_hash_table_lookup (c->identities, lang->data);
      group = g_strdup_printf("identity_%s", (gchar *)lang->data);
      g_key_file_set_string (key_file, group, "category", i->category);
      g_key_file_set_string (key_file, group, "type", i->type);
      g_key_file_set_string (key_file, group, "name", i->name);
      g_free (group);
    }
  }
  g_list_free (langs);

  features = g_hash_table_get_values (c->features);
  {
    GList *feature;
    gchar **string_list;
    gint i;

    i = g_list_length (features);
    string_list = g_new (gchar*, i + 1);
    i = 0;
    for (feature=features; feature; feature=feature->next) {
      string_list[i] = g_strdup(feature->data);
      ++i;
    }
    string_list[i] = NULL;

    g_key_file_set_string_list (key_file, "features", "features",
                                (const gchar**)string_list, i);
    g_strfreev (string_list);
  }
  g_list_free (features);

  forms = g_hash_table_get_keys(c->forms);
  {
    dataform *d;
    GList *form, *fields;
    gchar *group;
    for (form=forms; form; form=form->next) {
      d = g_hash_table_lookup (c->forms, form->data);
      group = g_strdup_printf ("form_%s", (gchar *)form->data);
      fields = g_hash_table_get_keys(d->fields);
      {
        GList *field;
        GList *values;
        for (field=fields; field; field=field->next) {
          values = g_hash_table_lookup (d->fields, field->data);
          {
            GList *value;
            gchar **string_list;
            gint i;
            i = g_list_length (values);
            string_list = g_new (gchar*, i + 1);
            i = 0;
            for (value=values; value; value=value->next) {
              string_list[i] = g_strdup(value->data);
              ++i;
            }
            string_list[i] = NULL;

            g_key_file_set_string_list (key_file, group, field->data,
                                        (const gchar**)string_list, i);

            g_strfreev (string_list);
          }
        }
      }
      g_list_free(fields);
      g_free (group);
    }
  }
  g_list_free (forms);

  {
    gchar *data;
    gsize length;
    data = g_key_file_to_data (key_file, &length, NULL);
    write (fd, data, length);
    g_free(data);
    close (fd);
  }

  g_key_file_free(key_file);
caps_copy_exists:
  g_free(file);
caps_copy_return:
  return;
}

/* Restore capabilities from GKeyFile. Hash is not verified afterwards */
gboolean caps_restore_from_persistent (const char* hash)
{
  gchar *file;
  GKeyFile *key_file;
  gchar **groups, **group;
  gboolean restored = FALSE;

  file = caps_get_filename (hash);
  if (!file)
    goto caps_restore_no_file;

  key_file = g_key_file_new ();
  if (!g_key_file_load_from_file (key_file, file, G_KEY_FILE_NONE, NULL))
    goto caps_restore_bad_file;

  caps_add(hash);

  groups = g_key_file_get_groups (key_file, NULL);
  for (group = groups; *group; ++group) {
    if (!g_strcmp0(*group, "features")) {
      gchar **features, **feature;
      features = g_key_file_get_string_list (key_file, *group, "features",
                                             NULL, NULL);
      for (feature = features; *feature; ++feature) {
        caps_add_feature(hash, *feature);
      }

      g_strfreev (features);
    } else if (g_str_has_prefix (*group, "identity_")) {
      gchar *category, *type, *name, *lang;

      category = g_key_file_get_string(key_file, *group, "category", NULL);
      type = g_key_file_get_string(key_file, *group, "type", NULL);
      name = g_key_file_get_string(key_file, *group, "name", NULL);
      lang = *group + 9; /* "identity_" */

      caps_add_identity(hash, category, name, type, lang);
      g_free(category);
      g_free(type);
      g_free(name);
    } else if (g_str_has_prefix (*group, "form_")) {
      gchar *formtype;
      gchar **fields, **field;
      formtype = *group + 5; /* "form_" */
      caps_add_dataform (hash, formtype);

      fields = g_key_file_get_keys(key_file, *group, NULL, NULL);
      for (field = fields; *field; ++field) {
        gchar **values, **value;
        values = g_key_file_get_string_list (key_file, *group, *field,
                                             NULL, NULL);
        for (value = values; *value; ++value) {
          caps_add_dataform_field (hash, formtype, *field, *value);
        }
        g_strfreev (values);
      }
      g_strfreev (fields);
    }
  }
  g_strfreev(groups);
  restored = TRUE;

caps_restore_bad_file:
  g_key_file_free (key_file);
  g_free (file);
caps_restore_no_file:
  return restored;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
