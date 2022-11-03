/*
Source Defaults
Copyright (C) 2022 Ian Rodriguez ianlemuelr@gmail.com

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <util/dstr.h>

#include "plugin-macros.generated.h"

// so that old sources don't return with an empty settings object,
// thus letting us distinguish between "new" sources and recreated sources due to undo/redo
#define ENCOUNTERED_KEY "com.source_defaults.encountered"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define COPY_PROPERTIES 0
#define COPY_FILTERS 1
#define COPY_AUDIO_MONITORING 2
#define COPY_VOLUME 3
#define COPY_MUTED 4
#define COPY_BALANCE 5
#define COPY_SYNC_OFFSET 6
#define COPY_AUDIO_TRACKS 7

extern bool loaded;

static const char *option_keys[] = {
	"copy_properties",  "copy_filters",      "copy_audio_monitoring",
	"copy_volume",      "copy_muted",        "copy_balance",
	"copy_sync_offset", "copy_audio_tracks",
};

static const char *option_labels[] = {
	"Properties",  "Filters",       "Audio Monitoring Type",
	"Volume",      "Muted/Unmuted", "Stereo Balance",
	"Sync Offset", "Audio Tracks",
};

struct source_defaults {
	obs_source_t *source; // the filter itself
	obs_source_t *parent_source;
	bool options[ARRAY_SIZE(option_keys)];
};

static void enum_filters(obs_source_t *src, obs_source_t *filter, void *param)
{
	UNUSED_PARAMETER(src);
	obs_source_t *dst = param;
	const char *filter_id = obs_source_get_unversioned_id(filter);
	if (strcmp(filter_id, "source_defaults_video") != 0 &&
	    strcmp(filter_id, "source_defaults_audio") != 0) {
		obs_source_copy_single_filter(dst, filter);
	}
}

static void log_changes(struct source_defaults *src, obs_source_t *dst)
{
	struct dstr log = {0};
	dstr_init(&log);
	dstr_cat(&log, "Applied ");

	bool first_bool = true;
	for (unsigned long i = 0; i < ARRAY_SIZE(option_keys); i++) {
		if (src->options[i]) {
			if (first_bool) {
				dstr_cat(&log, option_labels[i]);
				first_bool = false;
			} else {
				dstr_catf(&log, ", %s", option_labels[i]);
			}
		}
	}
	if (!first_bool) {
		dstr_catf(&log, " from '%s'",
			  obs_source_get_name(src->parent_source));
		dstr_catf(&log, " to '%s'", obs_source_get_name(dst));
		blog(LOG_INFO, "%s", log.array);
	}
	dstr_free(&log);
}

static void source_created_cb(void *data, calldata_t *cd)
{
	if (!loaded) {
		return;
	}
	struct source_defaults *src = data;
	obs_source_t *dst = (obs_source_t *)calldata_ptr(cd, "source");
	bool already_encountered;

	src->parent_source = obs_filter_get_parent(src->source);

	if (!src->parent_source) {
		blog(LOG_WARNING,
		     "Filter has no parent source, so new source was skipped.");
		return;
	}

	// should be same type
	if (strcmp(obs_source_get_id(src->parent_source),
		   obs_source_get_id(dst)) != 0) {
		return;
	}
	if (obs_source_get_type(dst) != OBS_SOURCE_TYPE_INPUT) {
		return;
	}

	/* We have to distinguish between new sources and 
	   sources that are only recreated due to undo, 
	   otherwise settings will be copied over to old sources
	   that are simply re-created. 
	   
	   However, if you delete a source that was created prior to having Source Defaults,
	   and then undo it, it will be considered a new source.
	   */
	obs_data_t *dst_properties = obs_source_get_settings(dst);
	blog(LOG_DEBUG, "dst json: %s", obs_data_get_json(dst_properties));
	already_encountered =
		obs_data_get_bool(dst_properties, ENCOUNTERED_KEY);
	if (!already_encountered) {
		const char *dst_properties_json =
			obs_data_get_json(dst_properties);
		already_encountered = strcmp(dst_properties_json, "{}") != 0;
		// If the new source has non-default settings (not "{}")
		// consider it already encountered, but still write the
		// ENCOUNTERED_KEY, so that if the user resets its properties
		// to default, next callback execution will see it as already encountered.
		if (already_encountered || !src->options[COPY_PROPERTIES]) {
			obs_data_set_bool(dst_properties, ENCOUNTERED_KEY,
					  true);
			obs_source_update(dst, dst_properties);
		}
	}
	obs_data_release(dst_properties);

	if (already_encountered)
		return;

	if (src->options[COPY_PROPERTIES]) {
		obs_data_t *settings =
			obs_source_get_settings(src->parent_source);
		if (!already_encountered) {
			obs_data_set_bool(dst_properties, ENCOUNTERED_KEY,
					  true);
		}
		obs_source_update(dst, settings);
		obs_data_release(settings);

#ifndef NDEBUG
		dst_properties = obs_source_get_settings(dst);
		blog(LOG_DEBUG, "dst json2: %s",
		     obs_data_get_json(dst_properties));
		obs_data_release(dst_properties);
#endif // !NDEBUG
	}
	if (src->options[COPY_FILTERS]) {
		obs_source_enum_filters(src->parent_source, enum_filters, dst);
	}
	if (src->options[COPY_AUDIO_MONITORING]) {
		enum obs_monitoring_type monitoring =
			obs_source_get_monitoring_type(src->parent_source);
		obs_source_set_monitoring_type(dst, monitoring);
	}
	if (src->options[COPY_VOLUME]) {
		float volume = obs_source_get_volume(src->parent_source);
		obs_source_set_volume(dst, volume);
	}
	if (src->options[COPY_MUTED]) {
		bool muted = obs_source_muted(src->parent_source);
		obs_source_set_muted(dst, muted);
	}
	if (src->options[COPY_BALANCE]) {
		float balance =
			obs_source_get_balance_value(src->parent_source);
		obs_source_set_balance_value(dst, balance);
	}
	if (src->options[COPY_SYNC_OFFSET]) {
		int64_t sync_offset =
			obs_source_get_sync_offset(src->parent_source);
		obs_source_set_sync_offset(dst, sync_offset);
	}
	if (src->options[COPY_AUDIO_TRACKS]) {
		uint32_t tracks =
			obs_source_get_audio_mixers(src->parent_source);
		obs_source_set_audio_mixers(dst, tracks);
	}
	log_changes(src, dst);
}

static void source_defaults_update(void *data, obs_data_t *settings)
{
	struct source_defaults *src = data;
	for (unsigned long i = 0; i < ARRAY_SIZE(option_keys); i++) {
		src->options[i] = obs_data_get_bool(settings, option_keys[i]);
	}
}

static const char *source_defaults_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Source Defaults");
}

static obs_properties_t *source_defaults_properties(void *data)
{
	struct source_defaults *src = data;
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(
		props, "description",
		"Tick the checkboxes for those that you want to be copied to newly created sources of the same type.",
		OBS_TEXT_INFO);

	for (int i = 0; i < 2; i++) {
		obs_properties_add_bool(props, option_keys[i],
					option_labels[i]);
	}

	src->parent_source = obs_filter_get_parent(src->source);
	if (obs_source_get_output_flags(src->parent_source) &
	    OBS_SOURCE_AUDIO) {
		for (unsigned long i = 2; i < ARRAY_SIZE(option_keys); i++) {
			obs_properties_add_bool(props, option_keys[i],
						option_labels[i]);
		}
	}

	return props;
}

static void source_defaults_get_defaults(obs_data_t *settings)
{
	for (unsigned long i = 0; i < ARRAY_SIZE(option_keys); i++) {
		obs_data_set_default_bool(settings, option_keys[i], true);
	}
}

static void _source_defaults_enable(struct source_defaults *src, bool enabled)
{
	signal_handler_t *sh = obs_get_signal_handler();
	if (enabled) {
		signal_handler_connect(sh, "source_create", source_created_cb,
				       src);
	} else {
		signal_handler_disconnect(sh, "source_create",
					  source_created_cb, src);
	}
}

static void source_defaults_enable(void *data, calldata_t *cd)
{
	struct source_defaults *src = data;
	bool enabled = calldata_bool(cd, "enabled");

	_source_defaults_enable(src, enabled);
}

static void source_defaults_show(void *data)
{
	struct source_defaults *src = data;
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_create", source_created_cb, src);
}

static void source_defaults_hide(void *data)
{
	struct source_defaults *src = data;
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_create", source_created_cb, src);
}

static void *source_defaults_create(obs_data_t *settings, obs_source_t *source)
{
	struct source_defaults *src = bzalloc(sizeof(struct source_defaults));
	src->source = source;

	source_defaults_update(src, settings);

	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_connect(sh, "enable", source_defaults_enable, src);

	if (!obs_source_is_hidden(src->source))
		_source_defaults_enable(src, true);

	return src;
}

static void source_defaults_destroy(void *data)
{
	struct source_defaults *src = data;
	signal_handler_t *sh = obs_source_get_signal_handler(src->source);
	signal_handler_disconnect(sh, "enable", source_defaults_enable, src);
	_source_defaults_enable(src, false);
	obs_source_release(src->source);
	obs_source_release(src->parent_source);
	bfree(data);
}

/* OBS doesn't allow creating a filter that will show up for both 
	video and audio filters, so we define two. */

struct obs_source_info source_defaults_video_info = {
	.id = "source_defaults_video",
	.version = 1,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.create = source_defaults_create,
	.destroy = source_defaults_destroy,
	.update = source_defaults_update,
	.get_name = source_defaults_get_name,
	.get_defaults = source_defaults_get_defaults,
	.get_properties = source_defaults_properties,
};

struct obs_source_info source_defaults_audio_info = {
	.id = "source_defaults_audio",
	.version = 1,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.create = source_defaults_create,
	.destroy = source_defaults_destroy,
	.update = source_defaults_update,
	.get_name = source_defaults_get_name,
	.get_defaults = source_defaults_get_defaults,
	.get_properties = source_defaults_properties,
};
