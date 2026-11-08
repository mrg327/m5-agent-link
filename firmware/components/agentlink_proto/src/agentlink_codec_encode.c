/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 3
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "zcbor_encode.h"
#include "agentlink_codec_encode.h"
#include "zcbor_print.h"

#if DEFAULT_MAX_QTY != 3
#error "The type file was generated with a different default_max_qty than this file"
#endif

#define log_result(state, result, func) do { \
	if (!result) { \
		zcbor_trace_file(state); \
		zcbor_log("%s error: %s\r\n", func, zcbor_error_str(zcbor_peek_error(state))); \
	} else { \
		zcbor_log("%s success\r\n", func); \
	} \
} while(0)

static bool encode_repeated_hdr_uint2uint(zcbor_state_t *state, const struct hdr_uint2uint *input);
static bool encode_hdr(zcbor_state_t *state, const struct hdr_r *input);
static bool encode_device_class(zcbor_state_t *state, const struct device_class_r *input);
static bool encode_device_info(zcbor_state_t *state, const struct device_info *input);
static bool encode_disp_shape(zcbor_state_t *state, const struct disp_shape_r *input);
static bool encode_display_caps(zcbor_state_t *state, const struct display_caps *input);
static bool encode_repeated_display_caps_m(zcbor_state_t *state, const struct display_caps_m *input);
static bool encode_input_kind(zcbor_state_t *state, const struct input_kind_r *input);
static bool encode_repeated_caps_uint2uint(zcbor_state_t *state, const struct caps_uint2uint *input);
static bool encode_feature(zcbor_state_t *state, const struct feature_r *input);
static bool encode_caps(zcbor_state_t *state, const struct caps *input);
static bool encode_repeated_hello_uint3uint(zcbor_state_t *state, const struct hello_uint3uint *input);
static bool encode_hello(zcbor_state_t *state, const struct hello *input);
static bool encode_msg_hello(zcbor_state_t *state, const struct msg_hello *input);
static bool encode_host_os(zcbor_state_t *state, const struct host_os_r *input);
static bool encode_host_info(zcbor_state_t *state, const struct host_info *input);
static bool encode_time_sync(zcbor_state_t *state, const struct time_sync *input);
static bool encode_privacy(zcbor_state_t *state, const struct privacy_r *input);
static bool encode_repeated_device_cfg_privacy_m(zcbor_state_t *state, const struct device_cfg_privacy_m *input);
static bool encode_alert_mode(zcbor_state_t *state, const struct alert_mode_r *input);
static bool encode_repeated_device_cfg_alert_mode_m(zcbor_state_t *state, const struct device_cfg_alert_mode_m *input);
static bool encode_repeated_device_cfg_uint2uint(zcbor_state_t *state, const struct device_cfg_uint2uint *input);
static bool encode_repeated_device_cfg_uint3uint(zcbor_state_t *state, const struct device_cfg_uint3uint *input);
static bool encode_repeated_device_cfg_uint4tstr(zcbor_state_t *state, const struct device_cfg_uint4tstr *input);
static bool encode_device_cfg(zcbor_state_t *state, const struct device_cfg *input);
static bool encode_welcome(zcbor_state_t *state, const struct welcome *input);
static bool encode_msg_welcome(zcbor_state_t *state, const struct msg_welcome *input);
static bool encode_agent(zcbor_state_t *state, const struct agent_r *input);
static bool encode_repeated_session_agent_m(zcbor_state_t *state, const struct session_agent_m *input);
static bool encode_sess_state(zcbor_state_t *state, const struct sess_state_r *input);
static bool encode_repeated_session_sess_state_m(zcbor_state_t *state, const struct session_sess_state_m *input);
static bool encode_input_sub(zcbor_state_t *state, const struct input_sub_r *input);
static bool encode_repeated_session_input_sub_m(zcbor_state_t *state, const struct session_input_sub_m *input);
static bool encode_repeated_session_uint4tstr(zcbor_state_t *state, const struct session_uint4tstr *input);
static bool encode_repeated_session_uint5tstr(zcbor_state_t *state, const struct session_uint5tstr *input);
static bool encode_repeated_session_uint6tstr(zcbor_state_t *state, const struct session_uint6tstr *input);
static bool encode_repeated_session_uint7uint(zcbor_state_t *state, const struct session_uint7uint *input);
static bool encode_repeated_session_uint8uint(zcbor_state_t *state, const struct session_uint8uint *input);
static bool encode_repeated_session_uint9tstr(zcbor_state_t *state, const struct session_uint9tstr *input);
static bool encode_repeated_session_uint10bool(zcbor_state_t *state, const struct session_uint10bool *input);
static bool encode_repeated_session_uint11uint(zcbor_state_t *state, const struct session_uint11uint *input);
static bool encode_session(zcbor_state_t *state, const struct session *input);
static bool encode_att_kind(zcbor_state_t *state, const struct att_kind_r *input);
static bool encode_away_event(zcbor_state_t *state, const struct away_event *input);
static bool encode_repeated_snapshot_away_event_m_l(zcbor_state_t *state, const struct snapshot_away_event_m_l_r *input);
static bool encode_snapshot(zcbor_state_t *state, const struct snapshot *input);
static bool encode_msg_snapshot(zcbor_state_t *state, const struct msg_snapshot *input);
static bool encode_msg_session_upd(zcbor_state_t *state, const struct msg_session_upd *input);
static bool encode_gone(zcbor_state_t *state, const struct gone *input);
static bool encode_msg_gone(zcbor_state_t *state, const struct msg_gone *input);
static bool encode_repeated_attention_uint3tstr(zcbor_state_t *state, const struct attention_uint3tstr *input);
static bool encode_repeated_attention_uint4tstr(zcbor_state_t *state, const struct attention_uint4tstr *input);
static bool encode_repeated_attention_tstr_l(zcbor_state_t *state, const struct attention_tstr_l_r *input);
static bool encode_action(zcbor_state_t *state, const struct action_r *input);
static bool encode_repeated_attention_uint7uint(zcbor_state_t *state, const struct attention_uint7uint *input);
static bool encode_repeated_attention_uint8uint(zcbor_state_t *state, const struct attention_uint8uint *input);
static bool encode_attention(zcbor_state_t *state, const struct attention *input);
static bool encode_msg_attention(zcbor_state_t *state, const struct msg_attention *input);
static bool encode_clear_why(zcbor_state_t *state, const struct clear_why_r *input);
static bool encode_clear(zcbor_state_t *state, const struct clear *input);
static bool encode_msg_clear(zcbor_state_t *state, const struct msg_clear *input);
static bool encode_cmd_op(zcbor_state_t *state, const struct cmd_op_r *input);
static bool encode_repeated_cmd_uint1uint(zcbor_state_t *state, const struct cmd_uint1uint *input);
static bool encode_repeated_cmd_uint2uint(zcbor_state_t *state, const struct cmd_uint2uint *input);
static bool encode_scroll_unit(zcbor_state_t *state, const struct scroll_unit_r *input);
static bool encode_repeated_scroll_arg_uint2uint(zcbor_state_t *state, const struct scroll_arg_uint2uint *input);
static bool encode_scroll_arg(zcbor_state_t *state, const struct scroll_arg *input);
static bool encode_cmd_arg(zcbor_state_t *state, const struct cmd_arg_r *input);
static bool encode_repeated_cmd_arg_m(zcbor_state_t *state, const struct cmd_arg_m *input);
static bool encode_repeated_cmd_uint4uint(zcbor_state_t *state, const struct cmd_uint4uint *input);
static bool encode_cmd(zcbor_state_t *state, const struct cmd *input);
static bool encode_msg_cmd(zcbor_state_t *state, const struct msg_cmd *input);
static bool encode_result_err(zcbor_state_t *state, const struct result_err_r *input);
static bool encode_repeated_result_err_m(zcbor_state_t *state, const struct result_err_m *input);
static bool encode_repeated_result_uint2tstr(zcbor_state_t *state, const struct result_uint2tstr *input);
static bool encode_repeated_scroll_pos_uint2uint(zcbor_state_t *state, const struct scroll_pos_uint2uint *input);
static bool encode_repeated_scroll_pos_uint3tstr(zcbor_state_t *state, const struct scroll_pos_uint3tstr *input);
static bool encode_scroll_pos(zcbor_state_t *state, const struct scroll_pos *input);
static bool encode_repeated_result_scroll_pos_m(zcbor_state_t *state, const struct result_scroll_pos_m *input);
static bool encode_result(zcbor_state_t *state, const struct result *input);
static bool encode_msg_result(zcbor_state_t *state, const struct msg_result *input);
static bool encode_repeated_detail_uint0uint(zcbor_state_t *state, const struct detail_uint0uint *input);
static bool encode_repeated_detail_uint1uint(zcbor_state_t *state, const struct detail_uint1uint *input);
static bool encode_detail(zcbor_state_t *state, const struct detail *input);
static bool encode_msg_detail(zcbor_state_t *state, const struct msg_detail *input);
static bool encode_msg_cfg(zcbor_state_t *state, const struct msg_cfg *input);
static bool encode_batt(zcbor_state_t *state, const struct batt *input);
static bool encode_msg_batt(zcbor_state_t *state, const struct msg_batt *input);
static bool encode_empty(zcbor_state_t *state, const void *input);
static bool encode_msg_ping(zcbor_state_t *state, const struct msg_ping *input);
static bool encode_msg_pong(zcbor_state_t *state, const struct msg_pong *input);
static bool encode_log_level(zcbor_state_t *state, const struct log_level_r *input);
static bool encode_log(zcbor_state_t *state, const struct log *input);
static bool encode_msg_log(zcbor_state_t *state, const struct msg_log *input);
static bool encode_audio_codec(zcbor_state_t *state, const struct audio_codec_r *input);
static bool encode_repeated_audio_start_uint2uint(zcbor_state_t *state, const struct audio_start_uint2uint *input);
static bool encode_audio_start(zcbor_state_t *state, const struct audio_start *input);
static bool encode_msg_audio_start(zcbor_state_t *state, const struct msg_audio_start *input);
static bool encode_audio(zcbor_state_t *state, const struct audio *input);
static bool encode_msg_audio(zcbor_state_t *state, const struct msg_audio *input);
static bool encode_audio_end(zcbor_state_t *state, const struct audio_end *input);
static bool encode_msg_audio_end(zcbor_state_t *state, const struct msg_audio_end *input);
static bool encode_transcript(zcbor_state_t *state, const struct transcript *input);
static bool encode_msg_transcript(zcbor_state_t *state, const struct msg_transcript *input);
static bool encode_msg_dbg_ui_state_req(zcbor_state_t *state, const struct msg_dbg_ui_state_req *input);
static bool encode_repeated_dbg_ui_state_uint1uint(zcbor_state_t *state, const struct dbg_ui_state_uint1uint *input);
static bool encode_repeated_dbg_ui_state_uint2uint(zcbor_state_t *state, const struct dbg_ui_state_uint2uint *input);
static bool encode_repeated_dbg_ui_state_uint5uint(zcbor_state_t *state, const struct dbg_ui_state_uint5uint *input);
static bool encode_dbg_ui_state(zcbor_state_t *state, const struct dbg_ui_state *input);
static bool encode_msg_dbg_ui_state(zcbor_state_t *state, const struct msg_dbg_ui_state *input);
static bool encode_msg_dbg_screenshot_req(zcbor_state_t *state, const struct msg_dbg_screenshot_req *input);
static bool encode_dbg_screenshot_chunk(zcbor_state_t *state, const struct dbg_screenshot_chunk *input);
static bool encode_msg_dbg_screenshot_chunk(zcbor_state_t *state, const struct msg_dbg_screenshot_chunk *input);
static bool encode_input_event(zcbor_state_t *state, const struct input_event_r *input);
static bool encode_repeated_dbg_inject_input_uint1int(zcbor_state_t *state, const struct dbg_inject_input_uint1int *input);
static bool encode_repeated_dbg_inject_input_uint2int(zcbor_state_t *state, const struct dbg_inject_input_uint2int *input);
static bool encode_dbg_inject_input(zcbor_state_t *state, const struct dbg_inject_input *input);
static bool encode_msg_dbg_inject_input(zcbor_state_t *state, const struct msg_dbg_inject_input *input);
static bool encode_dbg_set_log_level(zcbor_state_t *state, const struct dbg_set_log_level *input);
static bool encode_msg_dbg_set_log_level(zcbor_state_t *state, const struct msg_dbg_set_log_level *input);
static bool encode_envelope(zcbor_state_t *state, const struct envelope_r *input);


static bool encode_repeated_hdr_uint2uint(
		zcbor_state_t *state, const struct hdr_uint2uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).hdr_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_hdr(
		zcbor_state_t *state, const struct hdr_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((((zcbor_uint32_put(state, (1))))
	&& (zcbor_uint32_encode(state, (&(*input).hdr_uint1uint))))
	&& (!(*input).hdr_uint2uint_present || encode_repeated_hdr_uint2uint(state, (&(*input).hdr_uint2uint))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_device_class(
		zcbor_state_t *state, const struct device_class_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).device_class_choice == device_class_watch_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).device_class_choice == device_class_desk_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).device_class_choice == device_class_pad_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).device_class_choice == device_class_sim_m_c) ? ((zcbor_uint32_put(state, (3))))
	: (((*input).device_class_choice == device_class_other_m_c) ? ((zcbor_uint32_put(state, (15))))
	: false)))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_device_info(
		zcbor_state_t *state, const struct device_info *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (encode_device_class(state, (&(*input).device_info_device_class_m))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_tstr_encode(state, (&(*input).device_info_uint1tstr))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (zcbor_tstr_encode(state, (&(*input).device_info_uint2tstr))))
	&& (((zcbor_uint32_put(state, (3))))
	&& ((((*input).device_info_uint3bstr.len == 8)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))
	&& (zcbor_bstr_encode(state, (&(*input).device_info_uint3bstr))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_disp_shape(
		zcbor_state_t *state, const struct disp_shape_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).disp_shape_choice == disp_shape_round_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).disp_shape_choice == disp_shape_rect_m_c) ? ((zcbor_uint32_put(state, (1))))
	: false))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_display_caps(
		zcbor_state_t *state, const struct display_caps *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (encode_disp_shape(state, (&(*input).display_caps_disp_shape_m))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_uint32_encode(state, (&(*input).display_caps_uint1uint))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).display_caps_uint2uint))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (zcbor_bool_encode(state, (&(*input).display_caps_uint3bool))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_display_caps_m(
		zcbor_state_t *state, const struct display_caps_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (0))))
	&& (encode_display_caps(state, (&(*input).display_caps_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_input_kind(
		zcbor_state_t *state, const struct input_kind_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).input_kind_choice == input_kind_touch_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).input_kind_choice == input_kind_btn_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).input_kind_choice == input_kind_rotary_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).input_kind_choice == input_kind_imu_m_c) ? ((zcbor_uint32_put(state, (3))))
	: false))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_caps_uint2uint(
		zcbor_state_t *state, const struct caps_uint2uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).caps_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_feature(
		zcbor_state_t *state, const struct feature_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).feature_choice == feature_focus_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).feature_choice == feature_ack_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).feature_choice == feature_approve_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).feature_choice == feature_reply_m_c) ? ((zcbor_uint32_put(state, (3))))
	: (((*input).feature_choice == feature_voice_m_c) ? ((zcbor_uint32_put(state, (4))))
	: (((*input).feature_choice == feature_badge_m_c) ? ((zcbor_uint32_put(state, (5))))
	: (((*input).feature_choice == feature_debug_m_c) ? ((zcbor_uint32_put(state, (6))))
	: (((*input).feature_choice == feature_scroll_m_c) ? ((zcbor_uint32_put(state, (7))))
	: false))))))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_caps(
		zcbor_state_t *state, const struct caps *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 9) && (((!(*input).display_caps_m_present || encode_repeated_display_caps_m(state, (&(*input).display_caps_m)))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_list_start_encode(state, 4) && ((zcbor_multi_encode_minmax(0, 4, &(*input).caps_input_kind_m_l_input_kind_m_count, (zcbor_encoder_t *)encode_input_kind, state, (*&(*input).caps_input_kind_m_l_input_kind_m), sizeof(struct input_kind_r))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 4)))
	&& (!(*input).caps_uint2uint_present || encode_repeated_caps_uint2uint(state, (&(*input).caps_uint2uint)))
	&& (((zcbor_uint32_put(state, (3))))
	&& (zcbor_bool_encode(state, (&(*input).caps_uint3bool))))
	&& (((zcbor_uint32_put(state, (4))))
	&& (zcbor_bool_encode(state, (&(*input).caps_uint4bool))))
	&& (((zcbor_uint32_put(state, (5))))
	&& (zcbor_bool_encode(state, (&(*input).caps_uint5bool))))
	&& (((zcbor_uint32_put(state, (6))))
	&& (zcbor_bool_encode(state, (&(*input).caps_uint6bool))))
	&& (((zcbor_uint32_put(state, (7))))
	&& (zcbor_uint32_encode(state, (&(*input).caps_uint7uint))))
	&& (((zcbor_uint32_put(state, (8))))
	&& (zcbor_list_start_encode(state, 8) && ((zcbor_multi_encode_minmax(0, 8, &(*input).caps_feature_m_l_feature_m_count, (zcbor_encoder_t *)encode_feature, state, (*&(*input).caps_feature_m_l_feature_m), sizeof(struct feature_r))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 8)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 9))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_hello_uint3uint(
		zcbor_state_t *state, const struct hello_uint3uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (3))))
	&& (zcbor_uint32_encode(state, (&(*input).hello_uint3uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_hello(
		zcbor_state_t *state, const struct hello *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_list_start_encode(state, 2) && ((((zcbor_uint32_encode(state, (&(*input).hello_major_l_major))))
	&& ((zcbor_uint32_encode(state, (&(*input).hello_major_l_minor))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 2)))
	&& (((zcbor_uint32_put(state, (1))))
	&& (encode_device_info(state, (&(*input).hello_device_info_m))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (encode_caps(state, (&(*input).hello_caps_m))))
	&& (!(*input).hello_uint3uint_present || encode_repeated_hello_uint3uint(state, (&(*input).hello_uint3uint)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_hello(
		zcbor_state_t *state, const struct msg_hello *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (1))))
	&& ((encode_hdr(state, (&(*input).msg_hello_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_hello(state, (&(*input).msg_hello_hello_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_host_os(
		zcbor_state_t *state, const struct host_os_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).host_os_choice == host_os_mac_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).host_os_choice == host_os_win_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).host_os_choice == host_os_linux_m_c) ? ((zcbor_uint32_put(state, (2))))
	: false)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_host_info(
		zcbor_state_t *state, const struct host_info *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_uint32_put(state, (0))))
	&& ((((*input).host_info_uint0bstr.len == 8)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))
	&& (zcbor_bstr_encode(state, (&(*input).host_info_uint0bstr))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_tstr_encode(state, (&(*input).host_info_uint1tstr))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (encode_host_os(state, (&(*input).host_info_host_os_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_time_sync(
		zcbor_state_t *state, const struct time_sync *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 2) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).time_sync_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_int32_encode(state, (&(*input).time_sync_uint1int))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 2))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_privacy(
		zcbor_state_t *state, const struct privacy_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).privacy_choice == privacy_meta_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).privacy_choice == privacy_preview_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).privacy_choice == privacy_full_m_c) ? ((zcbor_uint32_put(state, (2))))
	: false)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_device_cfg_privacy_m(
		zcbor_state_t *state, const struct device_cfg_privacy_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (0))))
	&& (encode_privacy(state, (&(*input).device_cfg_privacy_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_alert_mode(
		zcbor_state_t *state, const struct alert_mode_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).alert_mode_choice == alert_mode_full_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).alert_mode_choice == alert_mode_quiet_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).alert_mode_choice == alert_mode_silent_m_c) ? ((zcbor_uint32_put(state, (2))))
	: false)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_device_cfg_alert_mode_m(
		zcbor_state_t *state, const struct device_cfg_alert_mode_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (1))))
	&& (encode_alert_mode(state, (&(*input).device_cfg_alert_mode_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_device_cfg_uint2uint(
		zcbor_state_t *state, const struct device_cfg_uint2uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).device_cfg_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_device_cfg_uint3uint(
		zcbor_state_t *state, const struct device_cfg_uint3uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (3))))
	&& (zcbor_uint32_encode(state, (&(*input).device_cfg_uint3uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_device_cfg_uint4tstr(
		zcbor_state_t *state, const struct device_cfg_uint4tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (4))))
	&& (zcbor_tstr_encode(state, (&(*input).device_cfg_uint4tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_device_cfg(
		zcbor_state_t *state, const struct device_cfg *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 5) && (((!(*input).device_cfg_privacy_m_present || encode_repeated_device_cfg_privacy_m(state, (&(*input).device_cfg_privacy_m)))
	&& (!(*input).device_cfg_alert_mode_m_present || encode_repeated_device_cfg_alert_mode_m(state, (&(*input).device_cfg_alert_mode_m)))
	&& (!(*input).device_cfg_uint2uint_present || encode_repeated_device_cfg_uint2uint(state, (&(*input).device_cfg_uint2uint)))
	&& (!(*input).device_cfg_uint3uint_present || encode_repeated_device_cfg_uint3uint(state, (&(*input).device_cfg_uint3uint)))
	&& (!(*input).device_cfg_uint4tstr_present || encode_repeated_device_cfg_uint4tstr(state, (&(*input).device_cfg_uint4tstr)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 5))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_welcome(
		zcbor_state_t *state, const struct welcome *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_list_start_encode(state, 2) && ((((zcbor_uint32_encode(state, (&(*input).welcome_major_l_major))))
	&& ((zcbor_uint32_encode(state, (&(*input).welcome_major_l_minor))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 2)))
	&& (((zcbor_uint32_put(state, (1))))
	&& (encode_host_info(state, (&(*input).welcome_host_info_m))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (encode_time_sync(state, (&(*input).welcome_time_sync_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_device_cfg(state, (&(*input).welcome_device_cfg_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_welcome(
		zcbor_state_t *state, const struct msg_welcome *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (2))))
	&& ((encode_hdr(state, (&(*input).msg_welcome_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_welcome(state, (&(*input).msg_welcome_welcome_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_agent(
		zcbor_state_t *state, const struct agent_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).agent_choice == agent_copilot_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).agent_choice == agent_claude_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).agent_choice == agent_cloud_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).agent_choice == agent_other_m_c) ? ((zcbor_uint32_put(state, (15))))
	: false))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_agent_m(
		zcbor_state_t *state, const struct session_agent_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (1))))
	&& (encode_agent(state, (&(*input).session_agent_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_sess_state(
		zcbor_state_t *state, const struct sess_state_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).sess_state_choice == sess_state_start_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).sess_state_choice == sess_state_work_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).sess_state_choice == sess_state_input_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).sess_state_choice == sess_state_error_m_c) ? ((zcbor_uint32_put(state, (3))))
	: (((*input).sess_state_choice == sess_state_done_m_c) ? ((zcbor_uint32_put(state, (4))))
	: (((*input).sess_state_choice == sess_state_idle_m_c) ? ((zcbor_uint32_put(state, (5))))
	: (((*input).sess_state_choice == sess_state_ended_m_c) ? ((zcbor_uint32_put(state, (6))))
	: false)))))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_sess_state_m(
		zcbor_state_t *state, const struct session_sess_state_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (encode_sess_state(state, (&(*input).session_sess_state_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_input_sub(
		zcbor_state_t *state, const struct input_sub_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).input_sub_choice == input_sub_approval_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).input_sub_choice == input_sub_question_m_c) ? ((zcbor_uint32_put(state, (1))))
	: false))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_input_sub_m(
		zcbor_state_t *state, const struct session_input_sub_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (3))))
	&& (encode_input_sub(state, (&(*input).session_input_sub_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_uint4tstr(
		zcbor_state_t *state, const struct session_uint4tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (4))))
	&& (zcbor_tstr_encode(state, (&(*input).session_uint4tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_uint5tstr(
		zcbor_state_t *state, const struct session_uint5tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (5))))
	&& (zcbor_tstr_encode(state, (&(*input).session_uint5tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_uint6tstr(
		zcbor_state_t *state, const struct session_uint6tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (6))))
	&& (zcbor_tstr_encode(state, (&(*input).session_uint6tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_uint7uint(
		zcbor_state_t *state, const struct session_uint7uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (7))))
	&& (zcbor_uint32_encode(state, (&(*input).session_uint7uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_uint8uint(
		zcbor_state_t *state, const struct session_uint8uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (8))))
	&& (zcbor_uint32_encode(state, (&(*input).session_uint8uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_uint9tstr(
		zcbor_state_t *state, const struct session_uint9tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (9))))
	&& (zcbor_tstr_encode(state, (&(*input).session_uint9tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_uint10bool(
		zcbor_state_t *state, const struct session_uint10bool *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (10))))
	&& (zcbor_bool_encode(state, (&(*input).session_uint10bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_session_uint11uint(
		zcbor_state_t *state, const struct session_uint11uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (11))))
	&& (zcbor_uint32_encode(state, (&(*input).session_uint11uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_session(
		zcbor_state_t *state, const struct session *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 12) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).session_uint0uint))))
	&& (!(*input).session_agent_m_present || encode_repeated_session_agent_m(state, (&(*input).session_agent_m)))
	&& (!(*input).session_sess_state_m_present || encode_repeated_session_sess_state_m(state, (&(*input).session_sess_state_m)))
	&& (!(*input).session_input_sub_m_present || encode_repeated_session_input_sub_m(state, (&(*input).session_input_sub_m)))
	&& (!(*input).session_uint4tstr_present || encode_repeated_session_uint4tstr(state, (&(*input).session_uint4tstr)))
	&& (!(*input).session_uint5tstr_present || encode_repeated_session_uint5tstr(state, (&(*input).session_uint5tstr)))
	&& (!(*input).session_uint6tstr_present || encode_repeated_session_uint6tstr(state, (&(*input).session_uint6tstr)))
	&& (!(*input).session_uint7uint_present || encode_repeated_session_uint7uint(state, (&(*input).session_uint7uint)))
	&& (!(*input).session_uint8uint_present || encode_repeated_session_uint8uint(state, (&(*input).session_uint8uint)))
	&& (!(*input).session_uint9tstr_present || encode_repeated_session_uint9tstr(state, (&(*input).session_uint9tstr)))
	&& (!(*input).session_uint10bool_present || encode_repeated_session_uint10bool(state, (&(*input).session_uint10bool)))
	&& (!(*input).session_uint11uint_present || encode_repeated_session_uint11uint(state, (&(*input).session_uint11uint)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 12))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_att_kind(
		zcbor_state_t *state, const struct att_kind_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).att_kind_choice == att_kind_approval_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).att_kind_choice == att_kind_question_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).att_kind_choice == att_kind_error_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).att_kind_choice == att_kind_done_m_c) ? ((zcbor_uint32_put(state, (3))))
	: false))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_away_event(
		zcbor_state_t *state, const struct away_event *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).away_event_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (encode_att_kind(state, (&(*input).away_event_att_kind_m))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).away_event_uint2uint))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_snapshot_away_event_m_l(
		zcbor_state_t *state, const struct snapshot_away_event_m_l_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (1))))
	&& (zcbor_list_start_encode(state, 16) && ((zcbor_multi_encode_minmax(0, 16, &(*input).snapshot_away_event_m_l_away_event_m_count, (zcbor_encoder_t *)encode_away_event, state, (*&(*input).snapshot_away_event_m_l_away_event_m), sizeof(struct away_event))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 16))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_snapshot(
		zcbor_state_t *state, const struct snapshot *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 2) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_list_start_encode(state, 32) && ((zcbor_multi_encode_minmax(0, 32, &(*input).snapshot_session_m_l_session_m_count, (zcbor_encoder_t *)encode_session, state, (*&(*input).snapshot_session_m_l_session_m), sizeof(struct session))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 32)))
	&& (!(*input).snapshot_away_event_m_l_present || encode_repeated_snapshot_away_event_m_l(state, (&(*input).snapshot_away_event_m_l)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 2))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_snapshot(
		zcbor_state_t *state, const struct msg_snapshot *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (3))))
	&& ((encode_hdr(state, (&(*input).msg_snapshot_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_snapshot(state, (&(*input).msg_snapshot_snapshot_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_session_upd(
		zcbor_state_t *state, const struct msg_session_upd *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (4))))
	&& ((encode_hdr(state, (&(*input).msg_session_upd_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_session(state, (&(*input).msg_session_upd_session_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_gone(
		zcbor_state_t *state, const struct gone *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 1) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).gone_uint0uint))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 1))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_gone(
		zcbor_state_t *state, const struct msg_gone *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (5))))
	&& ((encode_hdr(state, (&(*input).msg_gone_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_gone(state, (&(*input).msg_gone_gone_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_attention_uint3tstr(
		zcbor_state_t *state, const struct attention_uint3tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (3))))
	&& (zcbor_tstr_encode(state, (&(*input).attention_uint3tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_attention_uint4tstr(
		zcbor_state_t *state, const struct attention_uint4tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (4))))
	&& (zcbor_tstr_encode(state, (&(*input).attention_uint4tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_attention_tstr_l(
		zcbor_state_t *state, const struct attention_tstr_l_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (5))))
	&& (zcbor_list_start_encode(state, 4) && ((zcbor_multi_encode_minmax(0, 4, &(*input).attention_tstr_l_tstr_count, (zcbor_encoder_t *)zcbor_tstr_encode, state, (*&(*input).attention_tstr_l_tstr), sizeof(struct zcbor_string))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_action(
		zcbor_state_t *state, const struct action_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).action_choice == action_approve_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).action_choice == action_deny_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).action_choice == action_focus_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).action_choice == action_reply_m_c) ? ((zcbor_uint32_put(state, (3))))
	: (((*input).action_choice == action_ack_m_c) ? ((zcbor_uint32_put(state, (4))))
	: false)))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_attention_uint7uint(
		zcbor_state_t *state, const struct attention_uint7uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (7))))
	&& (zcbor_uint32_encode(state, (&(*input).attention_uint7uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_attention_uint8uint(
		zcbor_state_t *state, const struct attention_uint8uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (8))))
	&& (zcbor_uint32_encode(state, (&(*input).attention_uint8uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_attention(
		zcbor_state_t *state, const struct attention *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 9) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).attention_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_uint32_encode(state, (&(*input).attention_uint1uint))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (encode_att_kind(state, (&(*input).attention_att_kind_m))))
	&& (!(*input).attention_uint3tstr_present || encode_repeated_attention_uint3tstr(state, (&(*input).attention_uint3tstr)))
	&& (!(*input).attention_uint4tstr_present || encode_repeated_attention_uint4tstr(state, (&(*input).attention_uint4tstr)))
	&& (!(*input).attention_tstr_l_present || encode_repeated_attention_tstr_l(state, (&(*input).attention_tstr_l)))
	&& (((zcbor_uint32_put(state, (6))))
	&& (zcbor_list_start_encode(state, 5) && ((zcbor_multi_encode_minmax(0, 5, &(*input).attention_action_m_l_action_m_count, (zcbor_encoder_t *)encode_action, state, (*&(*input).attention_action_m_l_action_m), sizeof(struct action_r))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 5)))
	&& (!(*input).attention_uint7uint_present || encode_repeated_attention_uint7uint(state, (&(*input).attention_uint7uint)))
	&& (!(*input).attention_uint8uint_present || encode_repeated_attention_uint8uint(state, (&(*input).attention_uint8uint)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 9))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_attention(
		zcbor_state_t *state, const struct msg_attention *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (6))))
	&& ((encode_hdr(state, (&(*input).msg_attention_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_attention(state, (&(*input).msg_attention_attention_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_clear_why(
		zcbor_state_t *state, const struct clear_why_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).clear_why_choice == clear_why_answered_pc_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).clear_why_choice == clear_why_answered_dev_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).clear_why_choice == clear_why_expired_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).clear_why_choice == clear_why_cancelled_m_c) ? ((zcbor_uint32_put(state, (3))))
	: false))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_clear(
		zcbor_state_t *state, const struct clear *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 2) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).clear_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (encode_clear_why(state, (&(*input).clear_why_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 2))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_clear(
		zcbor_state_t *state, const struct msg_clear *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (7))))
	&& ((encode_hdr(state, (&(*input).msg_clear_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_clear(state, (&(*input).msg_clear_clear_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_cmd_op(
		zcbor_state_t *state, const struct cmd_op_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).cmd_op_choice == cmd_op_focus_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).cmd_op_choice == cmd_op_ack_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).cmd_op_choice == cmd_op_snooze_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).cmd_op_choice == cmd_op_approve_m_c) ? ((zcbor_uint32_put(state, (3))))
	: (((*input).cmd_op_choice == cmd_op_deny_m_c) ? ((zcbor_uint32_put(state, (4))))
	: (((*input).cmd_op_choice == cmd_op_reply_m_c) ? ((zcbor_uint32_put(state, (5))))
	: (((*input).cmd_op_choice == cmd_op_cancel_m_c) ? ((zcbor_uint32_put(state, (6))))
	: (((*input).cmd_op_choice == cmd_op_detail_m_c) ? ((zcbor_uint32_put(state, (7))))
	: (((*input).cmd_op_choice == cmd_op_notify_done_m_c) ? ((zcbor_uint32_put(state, (8))))
	: (((*input).cmd_op_choice == cmd_op_quiet_m_c) ? ((zcbor_uint32_put(state, (9))))
	: (((*input).cmd_op_choice == cmd_op_scroll_m_c) ? ((zcbor_uint32_put(state, (10))))
	: false)))))))))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_cmd_uint1uint(
		zcbor_state_t *state, const struct cmd_uint1uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (1))))
	&& (zcbor_uint32_encode(state, (&(*input).cmd_uint1uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_cmd_uint2uint(
		zcbor_state_t *state, const struct cmd_uint2uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).cmd_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_scroll_unit(
		zcbor_state_t *state, const struct scroll_unit_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).scroll_unit_choice == scroll_unit_scroll_lines_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).scroll_unit_choice == scroll_unit_scroll_pages_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).scroll_unit_choice == scroll_unit_scroll_to_end_m_c) ? ((zcbor_uint32_put(state, (2))))
	: false)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_scroll_arg_uint2uint(
		zcbor_state_t *state, const struct scroll_arg_uint2uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).scroll_arg_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_scroll_arg(
		zcbor_state_t *state, const struct scroll_arg *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_uint32_put(state, (0))))
	&& (encode_scroll_unit(state, (&(*input).scroll_arg_scroll_unit_m))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_int32_encode(state, (&(*input).scroll_arg_uint1int))))
	&& (!(*input).scroll_arg_uint2uint_present || encode_repeated_scroll_arg_uint2uint(state, (&(*input).scroll_arg_uint2uint)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_cmd_arg(
		zcbor_state_t *state, const struct cmd_arg_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).cmd_arg_choice == cmd_arg_uint_c) ? ((zcbor_uint32_encode(state, (&(*input).cmd_arg_uint))))
	: (((*input).cmd_arg_choice == cmd_arg_tstr_c) ? ((zcbor_tstr_encode(state, (&(*input).cmd_arg_tstr))))
	: (((*input).cmd_arg_choice == cmd_arg_scroll_arg_m_c) ? ((encode_scroll_arg(state, (&(*input).cmd_arg_scroll_arg_m))))
	: false)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_cmd_arg_m(
		zcbor_state_t *state, const struct cmd_arg_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (3))))
	&& (encode_cmd_arg(state, (&(*input).cmd_arg_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_cmd_uint4uint(
		zcbor_state_t *state, const struct cmd_uint4uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (4))))
	&& (zcbor_uint32_encode(state, (&(*input).cmd_uint4uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_cmd(
		zcbor_state_t *state, const struct cmd *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 5) && (((((zcbor_uint32_put(state, (0))))
	&& (encode_cmd_op(state, (&(*input).cmd_op_m))))
	&& (!(*input).cmd_uint1uint_present || encode_repeated_cmd_uint1uint(state, (&(*input).cmd_uint1uint)))
	&& (!(*input).cmd_uint2uint_present || encode_repeated_cmd_uint2uint(state, (&(*input).cmd_uint2uint)))
	&& (!(*input).cmd_arg_m_present || encode_repeated_cmd_arg_m(state, (&(*input).cmd_arg_m)))
	&& (!(*input).cmd_uint4uint_present || encode_repeated_cmd_uint4uint(state, (&(*input).cmd_uint4uint)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 5))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_cmd(
		zcbor_state_t *state, const struct msg_cmd *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (8))))
	&& ((encode_hdr(state, (&(*input).msg_cmd_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_cmd(state, (&(*input).msg_cmd_cmd_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_result_err(
		zcbor_state_t *state, const struct result_err_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).result_err_choice == result_err_stale_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).result_err_choice == result_err_denied_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).result_err_choice == result_err_unsupported_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).result_err_choice == result_err_no_window_m_c) ? ((zcbor_uint32_put(state, (3))))
	: (((*input).result_err_choice == result_err_timeout_m_c) ? ((zcbor_uint32_put(state, (4))))
	: (((*input).result_err_choice == result_err_internal_m_c) ? ((zcbor_uint32_put(state, (5))))
	: false))))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_result_err_m(
		zcbor_state_t *state, const struct result_err_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (1))))
	&& (encode_result_err(state, (&(*input).result_err_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_result_uint2tstr(
		zcbor_state_t *state, const struct result_uint2tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_tstr_encode(state, (&(*input).result_uint2tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_scroll_pos_uint2uint(
		zcbor_state_t *state, const struct scroll_pos_uint2uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).scroll_pos_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_scroll_pos_uint3tstr(
		zcbor_state_t *state, const struct scroll_pos_uint3tstr *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (3))))
	&& (zcbor_tstr_encode(state, (&(*input).scroll_pos_uint3tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_scroll_pos(
		zcbor_state_t *state, const struct scroll_pos *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).scroll_pos_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_bool_encode(state, (&(*input).scroll_pos_uint1bool))))
	&& (!(*input).scroll_pos_uint2uint_present || encode_repeated_scroll_pos_uint2uint(state, (&(*input).scroll_pos_uint2uint)))
	&& (!(*input).scroll_pos_uint3tstr_present || encode_repeated_scroll_pos_uint3tstr(state, (&(*input).scroll_pos_uint3tstr)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_result_scroll_pos_m(
		zcbor_state_t *state, const struct result_scroll_pos_m *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (3))))
	&& (encode_scroll_pos(state, (&(*input).result_scroll_pos_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_result(
		zcbor_state_t *state, const struct result *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_bool_encode(state, (&(*input).result_uint0bool))))
	&& (!(*input).result_err_m_present || encode_repeated_result_err_m(state, (&(*input).result_err_m)))
	&& (!(*input).result_uint2tstr_present || encode_repeated_result_uint2tstr(state, (&(*input).result_uint2tstr)))
	&& (!(*input).result_scroll_pos_m_present || encode_repeated_result_scroll_pos_m(state, (&(*input).result_scroll_pos_m)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_result(
		zcbor_state_t *state, const struct msg_result *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (9))))
	&& ((encode_hdr(state, (&(*input).msg_result_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_result(state, (&(*input).msg_result_result_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_detail_uint0uint(
		zcbor_state_t *state, const struct detail_uint0uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).detail_uint0uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_detail_uint1uint(
		zcbor_state_t *state, const struct detail_uint1uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (1))))
	&& (zcbor_uint32_encode(state, (&(*input).detail_uint1uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_detail(
		zcbor_state_t *state, const struct detail *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && (((!(*input).detail_uint0uint_present || encode_repeated_detail_uint0uint(state, (&(*input).detail_uint0uint)))
	&& (!(*input).detail_uint1uint_present || encode_repeated_detail_uint1uint(state, (&(*input).detail_uint1uint)))
	&& (((zcbor_uint32_put(state, (2))))
	&& (zcbor_tstr_encode(state, (&(*input).detail_uint2tstr))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_detail(
		zcbor_state_t *state, const struct msg_detail *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (10))))
	&& ((encode_hdr(state, (&(*input).msg_detail_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_detail(state, (&(*input).msg_detail_detail_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_cfg(
		zcbor_state_t *state, const struct msg_cfg *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (11))))
	&& ((encode_hdr(state, (&(*input).msg_cfg_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_device_cfg(state, (&(*input).msg_cfg_device_cfg_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_batt(
		zcbor_state_t *state, const struct batt *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 2) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).batt_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_bool_encode(state, (&(*input).batt_uint1bool))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 2))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_batt(
		zcbor_state_t *state, const struct msg_batt *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (12))))
	&& ((encode_hdr(state, (&(*input).msg_batt_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_batt(state, (&(*input).msg_batt_batt_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_empty(
		zcbor_state_t *state, const void *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 0) && zcbor_map_end_encode(state, 0))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_ping(
		zcbor_state_t *state, const struct msg_ping *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (13))))
	&& ((encode_hdr(state, (&(*input).msg_ping_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_empty(state, NULL)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_pong(
		zcbor_state_t *state, const struct msg_pong *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (14))))
	&& ((encode_hdr(state, (&(*input).msg_pong_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_empty(state, NULL)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_log_level(
		zcbor_state_t *state, const struct log_level_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).log_level_choice == log_level_err_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).log_level_choice == log_level_warn_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).log_level_choice == log_level_info_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).log_level_choice == log_level_debug_m_c) ? ((zcbor_uint32_put(state, (3))))
	: false))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_log(
		zcbor_state_t *state, const struct log *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 2) && (((((zcbor_uint32_put(state, (0))))
	&& (encode_log_level(state, (&(*input).log_level_m))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_tstr_encode(state, (&(*input).log_uint1tstr))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 2))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_log(
		zcbor_state_t *state, const struct msg_log *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (15))))
	&& ((encode_hdr(state, (&(*input).msg_log_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_log(state, (&(*input).msg_log_log_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_audio_codec(
		zcbor_state_t *state, const struct audio_codec_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).audio_codec_choice == audio_codec_adpcm16k_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).audio_codec_choice == audio_codec_opus16k_m_c) ? ((zcbor_uint32_put(state, (1))))
	: false))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_audio_start_uint2uint(
		zcbor_state_t *state, const struct audio_start_uint2uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).audio_start_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_audio_start(
		zcbor_state_t *state, const struct audio_start *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).audio_start_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (encode_audio_codec(state, (&(*input).audio_start_audio_codec_m))))
	&& (!(*input).audio_start_uint2uint_present || encode_repeated_audio_start_uint2uint(state, (&(*input).audio_start_uint2uint)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_audio_start(
		zcbor_state_t *state, const struct msg_audio_start *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (16))))
	&& ((encode_hdr(state, (&(*input).msg_audio_start_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_audio_start(state, (&(*input).msg_audio_start_audio_start_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_audio(
		zcbor_state_t *state, const struct audio *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).audio_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_uint32_encode(state, (&(*input).audio_uint1uint))))
	&& (((zcbor_uint32_put(state, (2))))
	&& ((((*input).audio_uint2bstr.len >= 0)
	&& ((*input).audio_uint2bstr.len <= 240)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))
	&& (zcbor_bstr_encode(state, (&(*input).audio_uint2bstr))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_audio(
		zcbor_state_t *state, const struct msg_audio *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (17))))
	&& ((encode_hdr(state, (&(*input).msg_audio_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_audio(state, (&(*input).msg_audio_audio_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_audio_end(
		zcbor_state_t *state, const struct audio_end *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 1) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).audio_end_uint0uint))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 1))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_audio_end(
		zcbor_state_t *state, const struct msg_audio_end *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (18))))
	&& ((encode_hdr(state, (&(*input).msg_audio_end_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_audio_end(state, (&(*input).msg_audio_end_audio_end_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_transcript(
		zcbor_state_t *state, const struct transcript *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).transcript_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_tstr_encode(state, (&(*input).transcript_uint1tstr))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).transcript_uint2uint))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_transcript(
		zcbor_state_t *state, const struct msg_transcript *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (19))))
	&& ((encode_hdr(state, (&(*input).msg_transcript_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_transcript(state, (&(*input).msg_transcript_transcript_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_dbg_ui_state_req(
		zcbor_state_t *state, const struct msg_dbg_ui_state_req *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (64))))
	&& ((encode_hdr(state, (&(*input).msg_dbg_ui_state_req_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_empty(state, NULL)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_dbg_ui_state_uint1uint(
		zcbor_state_t *state, const struct dbg_ui_state_uint1uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (1))))
	&& (zcbor_uint32_encode(state, (&(*input).dbg_ui_state_uint1uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_dbg_ui_state_uint2uint(
		zcbor_state_t *state, const struct dbg_ui_state_uint2uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).dbg_ui_state_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_dbg_ui_state_uint5uint(
		zcbor_state_t *state, const struct dbg_ui_state_uint5uint *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (5))))
	&& (zcbor_uint32_encode(state, (&(*input).dbg_ui_state_uint5uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_dbg_ui_state(
		zcbor_state_t *state, const struct dbg_ui_state *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 6) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_tstr_encode(state, (&(*input).dbg_ui_state_uint0tstr))))
	&& (!(*input).dbg_ui_state_uint1uint_present || encode_repeated_dbg_ui_state_uint1uint(state, (&(*input).dbg_ui_state_uint1uint)))
	&& (!(*input).dbg_ui_state_uint2uint_present || encode_repeated_dbg_ui_state_uint2uint(state, (&(*input).dbg_ui_state_uint2uint)))
	&& (((zcbor_uint32_put(state, (3))))
	&& (zcbor_list_start_encode(state, 8) && ((zcbor_multi_encode_minmax(0, 8, &(*input).dbg_ui_state_tstr_l_tstr_count, (zcbor_encoder_t *)zcbor_tstr_encode, state, (*&(*input).dbg_ui_state_tstr_l_tstr), sizeof(struct zcbor_string))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 8)))
	&& (((zcbor_uint32_put(state, (4))))
	&& (zcbor_tstr_encode(state, (&(*input).dbg_ui_state_uint4tstr))))
	&& (!(*input).dbg_ui_state_uint5uint_present || encode_repeated_dbg_ui_state_uint5uint(state, (&(*input).dbg_ui_state_uint5uint)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 6))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_dbg_ui_state(
		zcbor_state_t *state, const struct msg_dbg_ui_state *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (65))))
	&& ((encode_hdr(state, (&(*input).msg_dbg_ui_state_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_dbg_ui_state(state, (&(*input).msg_dbg_ui_state_dbg_ui_state_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_dbg_screenshot_req(
		zcbor_state_t *state, const struct msg_dbg_screenshot_req *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (66))))
	&& ((encode_hdr(state, (&(*input).msg_dbg_screenshot_req_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_empty(state, NULL)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_dbg_screenshot_chunk(
		zcbor_state_t *state, const struct dbg_screenshot_chunk *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 5) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_encode(state, (&(*input).dbg_screenshot_chunk_uint0uint))))
	&& (((zcbor_uint32_put(state, (1))))
	&& (zcbor_uint32_encode(state, (&(*input).dbg_screenshot_chunk_uint1uint))))
	&& (((zcbor_uint32_put(state, (2))))
	&& (zcbor_uint32_encode(state, (&(*input).dbg_screenshot_chunk_uint2uint))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (zcbor_bstr_encode(state, (&(*input).dbg_screenshot_chunk_uint3bstr))))
	&& (((zcbor_uint32_put(state, (4))))
	&& (zcbor_bool_encode(state, (&(*input).dbg_screenshot_chunk_uint4bool))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 5))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_dbg_screenshot_chunk(
		zcbor_state_t *state, const struct msg_dbg_screenshot_chunk *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (67))))
	&& ((encode_hdr(state, (&(*input).msg_dbg_screenshot_chunk_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_dbg_screenshot_chunk(state, (&(*input).msg_dbg_screenshot_chunk_dbg_screenshot_chunk_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_input_event(
		zcbor_state_t *state, const struct input_event_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).input_event_choice == input_event_tap_m_c) ? ((zcbor_uint32_put(state, (0))))
	: (((*input).input_event_choice == input_event_double_tap_m_c) ? ((zcbor_uint32_put(state, (1))))
	: (((*input).input_event_choice == input_event_long_press_m_c) ? ((zcbor_uint32_put(state, (2))))
	: (((*input).input_event_choice == input_event_swipe_up_m_c) ? ((zcbor_uint32_put(state, (3))))
	: (((*input).input_event_choice == input_event_swipe_down_m_c) ? ((zcbor_uint32_put(state, (4))))
	: (((*input).input_event_choice == input_event_swipe_left_m_c) ? ((zcbor_uint32_put(state, (5))))
	: (((*input).input_event_choice == input_event_swipe_right_m_c) ? ((zcbor_uint32_put(state, (6))))
	: (((*input).input_event_choice == input_event_btn_a_m_c) ? ((zcbor_uint32_put(state, (7))))
	: (((*input).input_event_choice == input_event_btn_b_m_c) ? ((zcbor_uint32_put(state, (8))))
	: (((*input).input_event_choice == input_event_btn_a_long_m_c) ? ((zcbor_uint32_put(state, (9))))
	: (((*input).input_event_choice == input_event_btn_b_long_m_c) ? ((zcbor_uint32_put(state, (10))))
	: (((*input).input_event_choice == input_event_lift_m_c) ? ((zcbor_uint32_put(state, (11))))
	: (((*input).input_event_choice == input_event_shake_m_c) ? ((zcbor_uint32_put(state, (12))))
	: false)))))))))))))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_dbg_inject_input_uint1int(
		zcbor_state_t *state, const struct dbg_inject_input_uint1int *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (1))))
	&& (zcbor_int32_encode(state, (&(*input).dbg_inject_input_uint1int)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_dbg_inject_input_uint2int(
		zcbor_state_t *state, const struct dbg_inject_input_uint2int *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_put(state, (2))))
	&& (zcbor_int32_encode(state, (&(*input).dbg_inject_input_uint2int)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_dbg_inject_input(
		zcbor_state_t *state, const struct dbg_inject_input *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_uint32_put(state, (0))))
	&& (encode_input_event(state, (&(*input).dbg_inject_input_input_event_m))))
	&& (!(*input).dbg_inject_input_uint1int_present || encode_repeated_dbg_inject_input_uint1int(state, (&(*input).dbg_inject_input_uint1int)))
	&& (!(*input).dbg_inject_input_uint2int_present || encode_repeated_dbg_inject_input_uint2int(state, (&(*input).dbg_inject_input_uint2int)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_dbg_inject_input(
		zcbor_state_t *state, const struct msg_dbg_inject_input *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (68))))
	&& ((encode_hdr(state, (&(*input).msg_dbg_inject_input_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_dbg_inject_input(state, (&(*input).msg_dbg_inject_input_dbg_inject_input_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_dbg_set_log_level(
		zcbor_state_t *state, const struct dbg_set_log_level *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 1) && (((((zcbor_uint32_put(state, (0))))
	&& (encode_log_level(state, (&(*input).dbg_set_log_level_log_level_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 1))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_msg_dbg_set_log_level(
		zcbor_state_t *state, const struct msg_dbg_set_log_level *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_uint32_put(state, (0))))
	&& (zcbor_uint32_put(state, (69))))
	&& ((encode_hdr(state, (&(*input).msg_dbg_set_log_level_hdr_m))))
	&& (((zcbor_uint32_put(state, (3))))
	&& (encode_dbg_set_log_level(state, (&(*input).msg_dbg_set_log_level_dbg_set_log_level_m))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_envelope(
		zcbor_state_t *state, const struct envelope_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((*input).envelope_choice == envelope_msg_hello_m_c) ? ((encode_msg_hello(state, (&(*input).envelope_msg_hello_m))))
	: (((*input).envelope_choice == envelope_msg_welcome_m_c) ? ((encode_msg_welcome(state, (&(*input).envelope_msg_welcome_m))))
	: (((*input).envelope_choice == envelope_msg_snapshot_m_c) ? ((encode_msg_snapshot(state, (&(*input).envelope_msg_snapshot_m))))
	: (((*input).envelope_choice == envelope_msg_session_upd_m_c) ? ((encode_msg_session_upd(state, (&(*input).envelope_msg_session_upd_m))))
	: (((*input).envelope_choice == envelope_msg_gone_m_c) ? ((encode_msg_gone(state, (&(*input).envelope_msg_gone_m))))
	: (((*input).envelope_choice == envelope_msg_attention_m_c) ? ((encode_msg_attention(state, (&(*input).envelope_msg_attention_m))))
	: (((*input).envelope_choice == envelope_msg_clear_m_c) ? ((encode_msg_clear(state, (&(*input).envelope_msg_clear_m))))
	: (((*input).envelope_choice == envelope_msg_cmd_m_c) ? ((encode_msg_cmd(state, (&(*input).envelope_msg_cmd_m))))
	: (((*input).envelope_choice == envelope_msg_result_m_c) ? ((encode_msg_result(state, (&(*input).envelope_msg_result_m))))
	: (((*input).envelope_choice == envelope_msg_detail_m_c) ? ((encode_msg_detail(state, (&(*input).envelope_msg_detail_m))))
	: (((*input).envelope_choice == envelope_msg_cfg_m_c) ? ((encode_msg_cfg(state, (&(*input).envelope_msg_cfg_m))))
	: (((*input).envelope_choice == envelope_msg_batt_m_c) ? ((encode_msg_batt(state, (&(*input).envelope_msg_batt_m))))
	: (((*input).envelope_choice == envelope_msg_ping_m_c) ? ((encode_msg_ping(state, (&(*input).envelope_msg_ping_m))))
	: (((*input).envelope_choice == envelope_msg_pong_m_c) ? ((encode_msg_pong(state, (&(*input).envelope_msg_pong_m))))
	: (((*input).envelope_choice == envelope_msg_log_m_c) ? ((encode_msg_log(state, (&(*input).envelope_msg_log_m))))
	: (((*input).envelope_choice == envelope_msg_audio_start_m_c) ? ((encode_msg_audio_start(state, (&(*input).envelope_msg_audio_start_m))))
	: (((*input).envelope_choice == envelope_msg_audio_m_c) ? ((encode_msg_audio(state, (&(*input).envelope_msg_audio_m))))
	: (((*input).envelope_choice == envelope_msg_audio_end_m_c) ? ((encode_msg_audio_end(state, (&(*input).envelope_msg_audio_end_m))))
	: (((*input).envelope_choice == envelope_msg_transcript_m_c) ? ((encode_msg_transcript(state, (&(*input).envelope_msg_transcript_m))))
	: (((*input).envelope_choice == envelope_msg_dbg_ui_state_req_m_c) ? ((encode_msg_dbg_ui_state_req(state, (&(*input).envelope_msg_dbg_ui_state_req_m))))
	: (((*input).envelope_choice == envelope_msg_dbg_ui_state_m_c) ? ((encode_msg_dbg_ui_state(state, (&(*input).envelope_msg_dbg_ui_state_m))))
	: (((*input).envelope_choice == envelope_msg_dbg_screenshot_req_m_c) ? ((encode_msg_dbg_screenshot_req(state, (&(*input).envelope_msg_dbg_screenshot_req_m))))
	: (((*input).envelope_choice == envelope_msg_dbg_screenshot_chunk_m_c) ? ((encode_msg_dbg_screenshot_chunk(state, (&(*input).envelope_msg_dbg_screenshot_chunk_m))))
	: (((*input).envelope_choice == envelope_msg_dbg_inject_input_m_c) ? ((encode_msg_dbg_inject_input(state, (&(*input).envelope_msg_dbg_inject_input_m))))
	: (((*input).envelope_choice == envelope_msg_dbg_set_log_level_m_c) ? ((encode_msg_dbg_set_log_level(state, (&(*input).envelope_msg_dbg_set_log_level_m))))
	: false)))))))))))))))))))))))))));

	log_result(state, res, __func__);
	return res;
}



int cbor_encode_envelope(
		uint8_t *payload, size_t payload_len,
		const struct envelope_r *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[8];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_envelope, sizeof(states) / sizeof(zcbor_state_t), 1);
}
