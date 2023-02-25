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
#include <obs-frontend-api.h>
#include <util/dstr.h>
#include <util/base.h>

#include "plugin-macros.generated.h"

// so that old sources don't return with an empty settings object,
// thus letting us distinguish between "new" sources and recreated sources due to undo/redo
#define ENCOUNTERED_KEY "com.source_defaults.encountered"

// source settings
#define COPY_PROPERTIES 0
#define COPY_FILTERS 1
#define COPY_AUDIO_MONITORING 2
#define COPY_VOLUME 3
#define COPY_MUTED 4
#define COPY_BALANCE 5
#define COPY_SYNC_OFFSET 6
#define COPY_AUDIO_TRACKS 7

// sceneitem settings
#define COPY_TRANSFORM 0
#define COPY_VISIBILITY 1
#define COPY_VISIBILITY_TRANSITIONS 2

#define S_SCENEITEM_SETTINGS "scene_item_settings"
#define T_SCENEITEM_SETTINGS "Scene item settings"

#define S_PARENT_SCENE "parent_scene"

#define T_PARENT_SCENE "Parent Scene"
#define T_PARENT_SCENE_LONG_DESC                                                         \
	"Select the parent scene of the source that has this filter. "                   \
	"The following settings of this source will be copied from the selected scene. " \
	"If you have duplicates of this source, it will be copied from the bottommost one."

extern bool loaded;

/* clang-format off */

// source settings
static const char *option_keys[] = {
	"copy_properties",
	"copy_filters",
	"copy_audio_monitoring",
	"copy_volume",
	"copy_muted",
	"copy_balance",
	"copy_sync_offset",
	"copy_audio_tracks",
};

// sceneitem settings
static const char *sceneitem_option_keys[] = {
	"copy_transform",
	"copy_visibility",
	"copy_visibility_transitions",
};

static const char *option_labels[] = {
	"Properties",
	"Filters",
	"Audio Monitoring Type",
	"Volume",
	"Muted/Unmuted",
	"Stereo Balance",
	"Sync Offset",
	"Audio Tracks",
};

static const char *sceneitem_option_labels[] = {
	"Transform",
	"Show/Hide",
	"Show/Hide Transitions",
};

/* clang-format on */
struct source_defaults {
	obs_source_t *source; // the filter itself
	obs_weak_source_t *parent_source_weak;
	obs_weak_source_t *dst_source_weak;
	obs_weak_source_t *parent_scene_weak;
	bool options[OBS_COUNTOF(option_keys)];
	bool sceneitem_options[OBS_COUNTOF(sceneitem_option_keys)];
	char *parent_scene_name;

	// for deferred sceneitem visibility, because toggling right away doesn't work
	obs_sceneitem_t *src_sceneitem;
	obs_sceneitem_t *dst_sceneitem;
};

struct sceneitem_find_data {
	obs_source_t *source_to_find;
	obs_sceneitem_t *found_sceneitem;
	obs_source_t *scene_to_skip;
};

/* Forward declarations */
static void source_defaults_frontend_event_cb(enum obs_frontend_event event,
					      void *data);

/************************/

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

static void fill_scene_list(obs_property_t *scene_list)
{
	char **scene_names = obs_frontend_get_scene_names();
	for (char **temp = scene_names; *temp; temp++) {
		obs_property_list_add_string(scene_list, *temp, *temp);
	}
	bfree(scene_names);
}

/**
 * Finds the first sceneitem that has the specified source, because the `source_create`
 * signal only returns the source.
 */
static bool find_source_sceneitem(obs_scene_t *scene, obs_sceneitem_t *item,
				  void *param)
{
	UNUSED_PARAMETER(scene);
	struct sceneitem_find_data *find_data = param;
	obs_source_t *current_source = obs_sceneitem_get_source(item);
	if (current_source == find_data->source_to_find) {
		obs_sceneitem_addref(item);
		find_data->found_sceneitem = item;
		return false;
	}
	return true;
}

static bool find_source_sceneitem_all_scenes(void *param, obs_source_t *scene)
{
	struct sceneitem_find_data *find_data = param;
	if (strcmp(obs_source_get_name(scene),
		   obs_source_get_name(find_data->scene_to_skip)) == 0)
		return true;
	obs_scene_enum_items(obs_scene_from_source(scene),
			     find_source_sceneitem, find_data);
	if (find_data->found_sceneitem)
		return false;
	return true;
}

static bool any_true(bool *booleans, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (booleans[i])
			return true;
	}
	return false;
}

static void copy_transform(obs_sceneitem_t *src, obs_sceneitem_t *dst)
{
	obs_sceneitem_defer_update_begin(dst);
	struct obs_transform_info info;
	obs_sceneitem_get_info(src, &info);
	obs_sceneitem_set_info(dst, &info);

	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(src, &crop);
	obs_sceneitem_set_crop(dst, &crop);
	obs_sceneitem_defer_update_end(dst);
}

static void copy_visibility_transitions(obs_sceneitem_t *src,
					obs_sceneitem_t *dst)
{
	struct dstr new_name = {0};
	const char *dst_name =
		obs_source_get_name(obs_sceneitem_get_source(dst));
	for (size_t i = 0; i < 2; i++) {
		dstr_copy(&new_name, dst_name);
		dstr_cat(&new_name,
			 i ? " Show Transition" : " Hide Transition");

		obs_source_t *transition = obs_sceneitem_get_transition(src, i);
		obs_source_t *transition_copy =
			obs_source_duplicate(transition, new_name.array, false);
		obs_sceneitem_set_transition(dst, i, transition_copy);
		obs_source_release(transition_copy);

		uint32_t duration =
			obs_sceneitem_get_transition_duration(src, i);
		obs_sceneitem_set_transition_duration(dst, i, duration);
	}
	dstr_free(&new_name);
}

static void log_changes(struct source_defaults *src, const char *src_name,
			const char *dst_name, bool sceneitem_settings)
{
	struct dstr log = {0};
	bool first_bool = true;
	dstr_init(&log);
	dstr_cat(&log, "Applied ");

	if (sceneitem_settings) {
		for (size_t i = 0; i < OBS_COUNTOF(sceneitem_option_keys);
		     i++) {
			if (src->sceneitem_options[i]) {
				if (first_bool) {
					dstr_cat(&log,
						 sceneitem_option_labels[i]);
					first_bool = false;
				} else {
					dstr_catf(&log, ", %s",
						  sceneitem_option_labels[i]);
				}
			}
		}
	} else {
		for (size_t i = 0; i < OBS_COUNTOF(option_keys); i++) {
			if (src->options[i]) {
				if (first_bool) {
					dstr_cat(&log, option_labels[i]);
					first_bool = false;
				} else {
					dstr_catf(&log, ", %s",
						  option_labels[i]);
				}
			}
		}
	}
	if (!first_bool) {
		dstr_catf(&log, " from '%s'", src_name);
		dstr_catf(&log, " to '%s'", dst_name);
		blog(LOG_INFO, "%s", log.array);
	}
	dstr_free(&log);
}

static void deferred_sceneitem_defaults(void *data)
{
	struct source_defaults *src = data;
	if (!src->src_sceneitem || !src->dst_sceneitem)
		return;
	if (src->sceneitem_options[COPY_VISIBILITY]) {
		bool visible = obs_sceneitem_visible(src->src_sceneitem);
		obs_sceneitem_set_visible(src->dst_sceneitem, visible);
		obs_sceneitem_release(src->src_sceneitem);
		obs_sceneitem_release(src->dst_sceneitem);
		src->src_sceneitem = NULL;
		src->dst_sceneitem = NULL;
	}
}

static void apply_sceneitem_defaults(void *data, obs_sceneitem_t *dst_sceneitem)
{
	struct source_defaults *src = data;
	// First get the parent scene of the default source
	obs_source_t *parent_scene_source =
		obs_weak_source_get_source(src->parent_scene_weak);
	obs_scene_t *parent_scene = obs_scene_from_source(parent_scene_source);
	obs_source_t *parent_source =
		obs_weak_source_get_source(src->parent_source_weak);
	const char *parent_source_name = obs_source_get_name(parent_source);
	const char *dst_source_name =
		obs_source_get_name(obs_sceneitem_get_source(dst_sceneitem));

	struct sceneitem_find_data *parent_find_data =
		bzalloc(sizeof(struct sceneitem_find_data));

	if (parent_scene) {
		parent_find_data->source_to_find = parent_source;
		obs_scene_enum_items(parent_scene, find_source_sceneitem,
				     parent_find_data);
		if (parent_find_data->found_sceneitem) {
			// Apply source defaults
			if (src->sceneitem_options[COPY_TRANSFORM]) {
				copy_transform(
					parent_find_data->found_sceneitem,
					dst_sceneitem);
			}
			if (src->sceneitem_options[COPY_VISIBILITY]) {
				// try to set it right away for less latency when possible
				bool visible = obs_sceneitem_visible(
					parent_find_data->found_sceneitem);
				obs_sceneitem_set_visible(dst_sceneitem,
							  visible);

				obs_sceneitem_release(src->src_sceneitem);
				obs_sceneitem_release(src->dst_sceneitem);
				src->src_sceneitem =
					parent_find_data->found_sceneitem;
				src->dst_sceneitem = dst_sceneitem;
				obs_sceneitem_addref(src->src_sceneitem);
				obs_sceneitem_addref(src->dst_sceneitem);
				obs_queue_task(OBS_TASK_GRAPHICS,
					       deferred_sceneitem_defaults, src,
					       false);
			}
			if (src->sceneitem_options[COPY_VISIBILITY_TRANSITIONS]) {
				copy_visibility_transitions(
					parent_find_data->found_sceneitem,
					dst_sceneitem);
			}

			log_changes(src, parent_source_name, dst_source_name,
				    true);
		} else {
			blog(LOG_WARNING,
			     "Selected parent scene '%s' does not contain '%s',"
			     " scene item settings not copied.",
			     src->parent_scene_name, parent_source_name);
		}
	} else {
		blog(LOG_WARNING,
		     "Parent scene '%s' not found, scene item settings not copied.",
		     src->parent_scene_name);
	}

	obs_sceneitem_release(parent_find_data->found_sceneitem);
	bfree(parent_find_data);
	obs_source_release(parent_scene_source);
	obs_source_release(parent_source);
}

static void parent_scene_destroyed(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(cd);
	struct source_defaults *src = data;
	bfree(src->parent_scene_name);
	src->parent_scene_name = bstrdup("");
	obs_weak_source_release(src->parent_scene_weak);
	src->parent_scene_weak = NULL;
	obs_source_save(src->source);
}

static void parent_scene_renamed(void *data, calldata_t *cd)
{
	struct source_defaults *src = data;
	bfree(src->parent_scene_name);
	src->parent_scene_name = bstrdup(calldata_string(cd, "new_name"));
	obs_source_save(src->source);
}

static void start_monitoring_parent_scene(struct source_defaults *src,
					  obs_scene_t *scene)
{
	obs_source_t *source = obs_scene_get_source(scene);
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_connect(sh, "destroy", parent_scene_destroyed, src);
	signal_handler_connect(sh, "rename", parent_scene_renamed, src);
}

static void stop_monitoring_parent_scene(struct source_defaults *src,
					 obs_scene_t *scene)
{
	obs_source_t *source = obs_scene_get_source(scene);
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_disconnect(sh, "destroy", parent_scene_destroyed, src);
	signal_handler_disconnect(sh, "rename", parent_scene_renamed, src);
}

static void scene_item_add_cb(void *data, calldata_t *cd)
{
	struct source_defaults *src = data;
	obs_source_t *dst_source =
		obs_weak_source_get_source(src->dst_source_weak);
	if (!dst_source)
		return;

	//obs_scene_t *scene = calldata_ptr(cd, "scene");
	obs_sceneitem_t *sceneitem = calldata_ptr(cd, "item");
	obs_source_t *sceneitem_source = obs_sceneitem_get_source(sceneitem);

	if (sceneitem_source == dst_source) {
		obs_weak_source_release(src->dst_source_weak);
		src->dst_source_weak = NULL;
		apply_sceneitem_defaults(src, sceneitem);
	}

	obs_source_release(dst_source);
}

static bool all_scenes_item_add(void *data, obs_source_t *scene)
{
	struct source_defaults *src = data;
	signal_handler_t *sh = obs_source_get_signal_handler(scene);
	signal_handler_connect(sh, "item_add", scene_item_add_cb, src);
	return true;
}

static bool all_scenes_item_add_disconnect(void *data, obs_source_t *scene)
{
	struct source_defaults *src = data;
	signal_handler_t *sh = obs_source_get_signal_handler(scene);
	signal_handler_disconnect(sh, "item_add", scene_item_add_cb, src);
	return true;
}

static void source_defaults_frontend_event_cb(enum obs_frontend_event event,
					      void *data)
{
	struct source_defaults *src = data;
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
	    event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		obs_enum_scenes(all_scenes_item_add, src);

		// set default scene after all sources are loaded
		obs_scene_t *parent_scene =
			obs_get_scene_by_name(src->parent_scene_name);
		obs_weak_source_release(src->parent_scene_weak);
		if (parent_scene) {
			src->parent_scene_weak = obs_source_get_weak_source(
				obs_scene_get_source(parent_scene));
			obs_scene_release(parent_scene);
			start_monitoring_parent_scene(src, parent_scene);
		}

		obs_frontend_remove_event_callback(
			source_defaults_frontend_event_cb, src);
	}
}

static void source_created_cb(void *data, calldata_t *cd)
{
	if (!loaded) {
		return;
	}
	struct source_defaults *src = data;
	obs_source_t *dst = (obs_source_t *)calldata_ptr(cd, "source");
	if (obs_source_get_type(dst) == OBS_SOURCE_TYPE_SCENE) {
		signal_handler_t *sh = obs_source_get_signal_handler(dst);
		signal_handler_connect(sh, "item_add", scene_item_add_cb, src);
		return;
	}
	bool already_encountered = false;

	obs_source_t *parent_source = obs_filter_get_parent(src->source);
	obs_weak_source_release(src->parent_source_weak);
	src->parent_source_weak = obs_source_get_weak_source(parent_source);
	if (!parent_source) {
		blog(LOG_WARNING,
		     "Filter has no parent source, so new source was skipped.");
		return;
	}

	// should be same type
	if (strcmp(obs_source_get_id(parent_source), obs_source_get_id(dst)) !=
	    0) {
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
		const char *dst_id = obs_source_get_unversioned_id(dst);

		// Except media sources because drag-and-drop already
		// sets properties before the signal is propagated
		if (strcmp(dst_id, "ffmpeg_source") != 0) {
			already_encountered =
				strcmp(dst_properties_json, "{}") != 0;
		}
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
		obs_data_t *settings = obs_source_get_settings(parent_source);
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
		obs_source_enum_filters(parent_source, enum_filters, dst);
	}
	if (src->options[COPY_AUDIO_MONITORING]) {
		enum obs_monitoring_type monitoring =
			obs_source_get_monitoring_type(parent_source);
		obs_source_set_monitoring_type(dst, monitoring);
	}
	if (src->options[COPY_VOLUME]) {
		float volume = obs_source_get_volume(parent_source);
		obs_source_set_volume(dst, volume);
	}
	if (src->options[COPY_MUTED]) {
		bool muted = obs_source_muted(parent_source);
		obs_source_set_muted(dst, muted);
	}
	if (src->options[COPY_BALANCE]) {
		float balance = obs_source_get_balance_value(parent_source);
		obs_source_set_balance_value(dst, balance);
	}
	if (src->options[COPY_SYNC_OFFSET]) {
		int64_t sync_offset = obs_source_get_sync_offset(parent_source);
		obs_source_set_sync_offset(dst, sync_offset);
	}
	if (src->options[COPY_AUDIO_TRACKS]) {
		uint32_t tracks = obs_source_get_audio_mixers(parent_source);
		obs_source_set_audio_mixers(dst, tracks);
	}
	if (any_true(src->sceneitem_options,
		     OBS_COUNTOF(src->sceneitem_options))) {
		obs_weak_source_release(src->dst_source_weak);
		src->dst_source_weak = obs_source_get_weak_source(dst);
	}
	log_changes(src, obs_source_get_name(parent_source),
		    obs_source_get_name(dst), false);
}

static void source_defaults_update(void *data, obs_data_t *settings)
{
	struct source_defaults *src = data;

	for (size_t i = 0; i < OBS_COUNTOF(option_keys); i++) {
		src->options[i] = obs_data_get_bool(settings, option_keys[i]);
	}
	for (size_t i = 0; i < OBS_COUNTOF(sceneitem_option_keys); i++) {
		src->sceneitem_options[i] =
			obs_data_get_bool(settings, sceneitem_option_keys[i]);
	}
	const char *new_name = obs_data_get_string(settings, S_PARENT_SCENE);
	bool parent_scene_changed = strcmp(new_name, src->parent_scene_name) !=
				    0;
	bfree(src->parent_scene_name);
	src->parent_scene_name = bstrdup(new_name);
	if (loaded && parent_scene_changed) {
		obs_source_t *old_scene_source =
			obs_weak_source_get_source(src->parent_scene_weak);
		if (old_scene_source) {
			obs_scene_t *old_scene =
				obs_scene_from_source(old_scene_source);
			stop_monitoring_parent_scene(src, old_scene);
			obs_source_release(old_scene_source);
		}

		obs_scene_t *parent_scene =
			obs_get_scene_by_name(src->parent_scene_name);
		obs_weak_source_release(src->parent_scene_weak);
		if (parent_scene) {
			src->parent_scene_weak = obs_source_get_weak_source(
				obs_scene_get_source(parent_scene));
			obs_scene_release(parent_scene);
			start_monitoring_parent_scene(src, parent_scene);
		}
	}
}

static void source_defaults_save(void *data, obs_data_t *settings)
{
	struct source_defaults *src = data;
	obs_data_set_string(settings, S_PARENT_SCENE, src->parent_scene_name);
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
	obs_properties_t *sceneitem_settings_group = obs_properties_create();

	obs_properties_add_text(
		props, "description",
		"Tick the checkboxes for those that you want to be copied to newly created sources of the same type.",
		OBS_TEXT_INFO);

	for (int i = 0; i < 2; i++) {
		obs_properties_add_bool(props, option_keys[i],
					option_labels[i]);
	}

	obs_source_t *parent_source = obs_filter_get_parent(src->source);
	obs_weak_source_release(src->parent_source_weak);
	src->parent_source_weak = obs_source_get_weak_source(parent_source);
	if (obs_source_get_output_flags(parent_source) & OBS_SOURCE_AUDIO) {
		for (size_t i = 2; i < OBS_COUNTOF(option_keys); i++) {
			obs_properties_add_bool(props, option_keys[i],
						option_labels[i]);
		}
	}
	obs_properties_add_group(props, S_SCENEITEM_SETTINGS,
				 T_SCENEITEM_SETTINGS, OBS_GROUP_NORMAL,
				 sceneitem_settings_group);

	obs_property_t *parent_scene_list = obs_properties_add_list(
		sceneitem_settings_group, S_PARENT_SCENE, T_PARENT_SCENE,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_long_description(parent_scene_list,
					  T_PARENT_SCENE_LONG_DESC);
	obs_property_list_add_string(parent_scene_list, "--select scene--", "");
	fill_scene_list(parent_scene_list);
	for (size_t i = 0; i < OBS_COUNTOF(sceneitem_option_keys); i++) {
		obs_properties_add_bool(sceneitem_settings_group,
					sceneitem_option_keys[i],
					sceneitem_option_labels[i]);
	}
	return props;
}

static void source_defaults_get_defaults(obs_data_t *settings)
{
	for (size_t i = 0; i < OBS_COUNTOF(option_keys); i++) {
		obs_data_set_default_bool(settings, option_keys[i], true);
	}
	for (size_t i = 0; i < OBS_COUNTOF(sceneitem_option_keys); i++) {
		obs_data_set_default_bool(settings, sceneitem_option_keys[i],
					  true);
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
	src->parent_scene_name = bstrdup("");

	source_defaults_update(src, settings);

	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_connect(sh, "enable", source_defaults_enable, src);

	if (!obs_source_is_hidden(src->source))
		_source_defaults_enable(src, true);

	if (loaded) {
		obs_enum_scenes(all_scenes_item_add, src);
	} else {
		obs_frontend_add_event_callback(
			source_defaults_frontend_event_cb, src);
	}

	return src;
}

static void source_defaults_destroy(void *data)
{
	struct source_defaults *src = data;
	obs_enum_scenes(all_scenes_item_add_disconnect, src);
	signal_handler_t *sh = obs_source_get_signal_handler(src->source);
	signal_handler_disconnect(sh, "enable", source_defaults_enable, src);
	_source_defaults_enable(src, false);
	obs_source_release(src->source);
	obs_weak_source_release(src->parent_source_weak);
	obs_weak_source_release(src->parent_scene_weak);
	obs_weak_source_release(src->dst_source_weak);
	bfree(src->parent_scene_name);
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
	.save = source_defaults_save,
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
	.save = source_defaults_save,
	.get_name = source_defaults_get_name,
	.get_defaults = source_defaults_get_defaults,
	.get_properties = source_defaults_properties,
};
