/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 3
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "zcbor_decode.h"
#include "agentlink_codec_decode.h"
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

static bool decode_repeated_hdr_uint2uint(zcbor_state_t *state, struct hdr_uint2uint *result);
static bool decode_hdr(zcbor_state_t *state, struct hdr_r *result);
static bool decode_device_class(zcbor_state_t *state, struct device_class_r *result);
static bool decode_device_info(zcbor_state_t *state, struct device_info *result);
static bool decode_disp_shape(zcbor_state_t *state, struct disp_shape_r *result);
static bool decode_display_caps(zcbor_state_t *state, struct display_caps *result);
static bool decode_repeated_display_caps_m(zcbor_state_t *state, struct display_caps_m *result);
static bool decode_input_kind(zcbor_state_t *state, struct input_kind_r *result);
static bool decode_repeated_caps_uint2uint(zcbor_state_t *state, struct caps_uint2uint *result);
static bool decode_feature(zcbor_state_t *state, struct feature_r *result);
static bool decode_caps(zcbor_state_t *state, struct caps *result);
static bool decode_repeated_hello_uint3uint(zcbor_state_t *state, struct hello_uint3uint *result);
static bool decode_hello(zcbor_state_t *state, struct hello *result);
static bool decode_msg_hello(zcbor_state_t *state, struct msg_hello *result);
static bool decode_host_os(zcbor_state_t *state, struct host_os_r *result);
static bool decode_host_info(zcbor_state_t *state, struct host_info *result);
static bool decode_time_sync(zcbor_state_t *state, struct time_sync *result);
static bool decode_privacy(zcbor_state_t *state, struct privacy_r *result);
static bool decode_repeated_device_cfg_privacy_m(zcbor_state_t *state, struct device_cfg_privacy_m *result);
static bool decode_alert_mode(zcbor_state_t *state, struct alert_mode_r *result);
static bool decode_repeated_device_cfg_alert_mode_m(zcbor_state_t *state, struct device_cfg_alert_mode_m *result);
static bool decode_repeated_device_cfg_uint2uint(zcbor_state_t *state, struct device_cfg_uint2uint *result);
static bool decode_repeated_device_cfg_uint3uint(zcbor_state_t *state, struct device_cfg_uint3uint *result);
static bool decode_repeated_device_cfg_uint4tstr(zcbor_state_t *state, struct device_cfg_uint4tstr *result);
static bool decode_device_cfg(zcbor_state_t *state, struct device_cfg *result);
static bool decode_repeated_welcome_tstr_l(zcbor_state_t *state, struct welcome_tstr_l_r *result);
static bool decode_repeated_welcome_uint5bool(zcbor_state_t *state, struct welcome_uint5bool *result);
static bool decode_repeated_welcome_uint6bool(zcbor_state_t *state, struct welcome_uint6bool *result);
static bool decode_repeated_welcome_uint7bool(zcbor_state_t *state, struct welcome_uint7bool *result);
static bool decode_welcome(zcbor_state_t *state, struct welcome *result);
static bool decode_msg_welcome(zcbor_state_t *state, struct msg_welcome *result);
static bool decode_agent(zcbor_state_t *state, struct agent_r *result);
static bool decode_repeated_session_agent_m(zcbor_state_t *state, struct session_agent_m *result);
static bool decode_sess_state(zcbor_state_t *state, struct sess_state_r *result);
static bool decode_repeated_session_sess_state_m(zcbor_state_t *state, struct session_sess_state_m *result);
static bool decode_input_sub(zcbor_state_t *state, struct input_sub_r *result);
static bool decode_repeated_session_input_sub_m(zcbor_state_t *state, struct session_input_sub_m *result);
static bool decode_repeated_session_uint4tstr(zcbor_state_t *state, struct session_uint4tstr *result);
static bool decode_repeated_session_uint5tstr(zcbor_state_t *state, struct session_uint5tstr *result);
static bool decode_repeated_session_uint6tstr(zcbor_state_t *state, struct session_uint6tstr *result);
static bool decode_repeated_session_uint7uint(zcbor_state_t *state, struct session_uint7uint *result);
static bool decode_repeated_session_uint8uint(zcbor_state_t *state, struct session_uint8uint *result);
static bool decode_repeated_session_uint9tstr(zcbor_state_t *state, struct session_uint9tstr *result);
static bool decode_repeated_session_uint10bool(zcbor_state_t *state, struct session_uint10bool *result);
static bool decode_repeated_session_uint11uint(zcbor_state_t *state, struct session_uint11uint *result);
static bool decode_repeated_session_uint12tstr(zcbor_state_t *state, struct session_uint12tstr *result);
static bool decode_repeated_session_uint13uint(zcbor_state_t *state, struct session_uint13uint *result);
static bool decode_repeated_session_uint14bool(zcbor_state_t *state, struct session_uint14bool *result);
static bool decode_repeated_session_uint15uint(zcbor_state_t *state, struct session_uint15uint *result);
static bool decode_repeated_session_uint16bool(zcbor_state_t *state, struct session_uint16bool *result);
static bool decode_repeated_session_uint17tstr(zcbor_state_t *state, struct session_uint17tstr *result);
static bool decode_repeated_session_tstr_l(zcbor_state_t *state, struct session_tstr_l_r *result);
static bool decode_repeated_session_uint19uint(zcbor_state_t *state, struct session_uint19uint *result);
static bool decode_repeated_session_uint20uint(zcbor_state_t *state, struct session_uint20uint *result);
static bool decode_repeated_session_uint21uint(zcbor_state_t *state, struct session_uint21uint *result);
static bool decode_repeated_session_uint22tstr(zcbor_state_t *state, struct session_uint22tstr *result);
static bool decode_perm_mode(zcbor_state_t *state, struct perm_mode_r *result);
static bool decode_repeated_session_perm_mode_m(zcbor_state_t *state, struct session_perm_mode_m *result);
static bool decode_repeated_session_uint24uint(zcbor_state_t *state, struct session_uint24uint *result);
static bool decode_pr_state(zcbor_state_t *state, struct pr_state_r *result);
static bool decode_repeated_session_pr_state_m(zcbor_state_t *state, struct session_pr_state_m *result);
static bool decode_repeated_session_uint26bool(zcbor_state_t *state, struct session_uint26bool *result);
static bool decode_repeated_session_uint30uint(zcbor_state_t *state, struct session_uint30uint *result);
static bool decode_repeated_session_uint31uint(zcbor_state_t *state, struct session_uint31uint *result);
static bool decode_repeated_session_uint32uint(zcbor_state_t *state, struct session_uint32uint *result);
static bool decode_repeated_session_uint33bool(zcbor_state_t *state, struct session_uint33bool *result);
static bool decode_repeated_session_uint34uint(zcbor_state_t *state, struct session_uint34uint *result);
static bool decode_repeated_session_uint35tstr(zcbor_state_t *state, struct session_uint35tstr *result);
static bool decode_repeated_session_uint36uint(zcbor_state_t *state, struct session_uint36uint *result);
static bool decode_repeated_session_uint37uint(zcbor_state_t *state, struct session_uint37uint *result);
static bool decode_repeated_session_uint38tstr(zcbor_state_t *state, struct session_uint38tstr *result);
static bool decode_session(zcbor_state_t *state, struct session *result);
static bool decode_att_kind(zcbor_state_t *state, struct att_kind_r *result);
static bool decode_away_event(zcbor_state_t *state, struct away_event *result);
static bool decode_repeated_snapshot_away_event_m_l(zcbor_state_t *state, struct snapshot_away_event_m_l_r *result);
static bool decode_snapshot(zcbor_state_t *state, struct snapshot *result);
static bool decode_msg_snapshot(zcbor_state_t *state, struct msg_snapshot *result);
static bool decode_msg_session_upd(zcbor_state_t *state, struct msg_session_upd *result);
static bool decode_gone(zcbor_state_t *state, struct gone *result);
static bool decode_msg_gone(zcbor_state_t *state, struct msg_gone *result);
static bool decode_repeated_attention_uint3tstr(zcbor_state_t *state, struct attention_uint3tstr *result);
static bool decode_repeated_attention_uint4tstr(zcbor_state_t *state, struct attention_uint4tstr *result);
static bool decode_repeated_attention_tstr_l(zcbor_state_t *state, struct attention_tstr_l_r *result);
static bool decode_action(zcbor_state_t *state, struct action_r *result);
static bool decode_repeated_attention_uint7uint(zcbor_state_t *state, struct attention_uint7uint *result);
static bool decode_repeated_attention_uint8uint(zcbor_state_t *state, struct attention_uint8uint *result);
static bool decode_repeated_attention_uint9uint(zcbor_state_t *state, struct attention_uint9uint *result);
static bool decode_repeated_attention_uint10uint(zcbor_state_t *state, struct attention_uint10uint *result);
static bool decode_repeated_attention_uint11uint(zcbor_state_t *state, struct attention_uint11uint *result);
static bool decode_repeated_attention_uint12bool(zcbor_state_t *state, struct attention_uint12bool *result);
static bool decode_repeated_attention_uint13tstr(zcbor_state_t *state, struct attention_uint13tstr *result);
static bool decode_repeated_attention_uint14uint(zcbor_state_t *state, struct attention_uint14uint *result);
static bool decode_repeated_attention_uint15bool(zcbor_state_t *state, struct attention_uint15bool *result);
static bool decode_repeated_ask_opt_uint1tstr(zcbor_state_t *state, struct ask_opt_uint1tstr *result);
static bool decode_ask_opt(zcbor_state_t *state, struct ask_opt *result);
static bool decode_repeated_ask_q_uint3bool(zcbor_state_t *state, struct ask_q_uint3bool *result);
static bool decode_ask_q(zcbor_state_t *state, struct ask_q *result);
static bool decode_repeated_attention_ask_q_m_l(zcbor_state_t *state, struct attention_ask_q_m_l_r *result);
static bool decode_attention(zcbor_state_t *state, struct attention *result);
static bool decode_msg_attention(zcbor_state_t *state, struct msg_attention *result);
static bool decode_clear_why(zcbor_state_t *state, struct clear_why_r *result);
static bool decode_clear(zcbor_state_t *state, struct clear *result);
static bool decode_msg_clear(zcbor_state_t *state, struct msg_clear *result);
static bool decode_cmd_op(zcbor_state_t *state, struct cmd_op_r *result);
static bool decode_repeated_cmd_uint1uint(zcbor_state_t *state, struct cmd_uint1uint *result);
static bool decode_repeated_cmd_uint2uint(zcbor_state_t *state, struct cmd_uint2uint *result);
static bool decode_scroll_unit(zcbor_state_t *state, struct scroll_unit_r *result);
static bool decode_repeated_scroll_arg_uint2uint(zcbor_state_t *state, struct scroll_arg_uint2uint *result);
static bool decode_scroll_arg(zcbor_state_t *state, struct scroll_arg *result);
static bool decode_cmd_arg(zcbor_state_t *state, struct cmd_arg_r *result);
static bool decode_repeated_cmd_arg_m(zcbor_state_t *state, struct cmd_arg_m *result);
static bool decode_repeated_cmd_uint4uint(zcbor_state_t *state, struct cmd_uint4uint *result);
static bool decode_repeated_cmd_uint5uint(zcbor_state_t *state, struct cmd_uint5uint *result);
static bool decode_repeated_cmd_uint6bool(zcbor_state_t *state, struct cmd_uint6bool *result);
static bool decode_answer_picks(zcbor_state_t *state, struct answer_picks *result);
static bool decode_repeated_cmd_answer_picks_m(zcbor_state_t *state, struct cmd_answer_picks_m *result);
static bool decode_answer_aids(zcbor_state_t *state, struct answer_aids *result);
static bool decode_repeated_cmd_answer_aids_m(zcbor_state_t *state, struct cmd_answer_aids_m *result);
static bool decode_repeated_pointer_arg_uint0int(zcbor_state_t *state, struct pointer_arg_uint0int *result);
static bool decode_repeated_pointer_arg_uint1int(zcbor_state_t *state, struct pointer_arg_uint1int *result);
static bool decode_pointer_btn(zcbor_state_t *state, struct pointer_btn_r *result);
static bool decode_repeated_pointer_arg_pointer_btn_m(zcbor_state_t *state, struct pointer_arg_pointer_btn_m *result);
static bool decode_repeated_pointer_arg_uint3uint(zcbor_state_t *state, struct pointer_arg_uint3uint *result);
static bool decode_repeated_pointer_arg_uint4int(zcbor_state_t *state, struct pointer_arg_uint4int *result);
static bool decode_pointer_arg(zcbor_state_t *state, struct pointer_arg *result);
static bool decode_repeated_cmd_pointer_arg_m(zcbor_state_t *state, struct cmd_pointer_arg_m *result);
static bool decode_cmd(zcbor_state_t *state, struct cmd *result);
static bool decode_msg_cmd(zcbor_state_t *state, struct msg_cmd *result);
static bool decode_result_err(zcbor_state_t *state, struct result_err_r *result);
static bool decode_repeated_result_err_m(zcbor_state_t *state, struct result_err_m *result);
static bool decode_repeated_result_uint2tstr(zcbor_state_t *state, struct result_uint2tstr *result);
static bool decode_repeated_scroll_pos_uint2uint(zcbor_state_t *state, struct scroll_pos_uint2uint *result);
static bool decode_repeated_scroll_pos_uint3tstr(zcbor_state_t *state, struct scroll_pos_uint3tstr *result);
static bool decode_scroll_pos(zcbor_state_t *state, struct scroll_pos *result);
static bool decode_repeated_result_scroll_pos_m(zcbor_state_t *state, struct result_scroll_pos_m *result);
static bool decode_result(zcbor_state_t *state, struct result *result);
static bool decode_msg_result(zcbor_state_t *state, struct msg_result *result);
static bool decode_repeated_detail_uint0uint(zcbor_state_t *state, struct detail_uint0uint *result);
static bool decode_repeated_detail_uint1uint(zcbor_state_t *state, struct detail_uint1uint *result);
static bool decode_repeated_detail_uint3uint(zcbor_state_t *state, struct detail_uint3uint *result);
static bool decode_repeated_detail_uint4uint(zcbor_state_t *state, struct detail_uint4uint *result);
static bool decode_repeated_detail_uint5uint(zcbor_state_t *state, struct detail_uint5uint *result);
static bool decode_detail(zcbor_state_t *state, struct detail *result);
static bool decode_msg_detail(zcbor_state_t *state, struct msg_detail *result);
static bool decode_msg_cfg(zcbor_state_t *state, struct msg_cfg *result);
static bool decode_batt(zcbor_state_t *state, struct batt *result);
static bool decode_msg_batt(zcbor_state_t *state, struct msg_batt *result);
static bool decode_empty(zcbor_state_t *state, void *result);
static bool decode_msg_ping(zcbor_state_t *state, struct msg_ping *result);
static bool decode_msg_pong(zcbor_state_t *state, struct msg_pong *result);
static bool decode_log_level(zcbor_state_t *state, struct log_level_r *result);
static bool decode_log(zcbor_state_t *state, struct log *result);
static bool decode_msg_log(zcbor_state_t *state, struct msg_log *result);
static bool decode_audio_codec(zcbor_state_t *state, struct audio_codec_r *result);
static bool decode_repeated_audio_start_uint2uint(zcbor_state_t *state, struct audio_start_uint2uint *result);
static bool decode_audio_start(zcbor_state_t *state, struct audio_start *result);
static bool decode_msg_audio_start(zcbor_state_t *state, struct msg_audio_start *result);
static bool decode_audio(zcbor_state_t *state, struct audio *result);
static bool decode_msg_audio(zcbor_state_t *state, struct msg_audio *result);
static bool decode_audio_end(zcbor_state_t *state, struct audio_end *result);
static bool decode_msg_audio_end(zcbor_state_t *state, struct msg_audio_end *result);
static bool decode_transcript(zcbor_state_t *state, struct transcript *result);
static bool decode_msg_transcript(zcbor_state_t *state, struct msg_transcript *result);
static bool decode_kb_start(zcbor_state_t *state, struct kb_start *result);
static bool decode_msg_kb_start(zcbor_state_t *state, struct msg_kb_start *result);
static bool decode_kb_stroke(zcbor_state_t *state, struct kb_stroke *result);
static bool decode_msg_kb_stroke(zcbor_state_t *state, struct msg_kb_stroke *result);
static bool decode_repeated_kb_cands_uint3uint(zcbor_state_t *state, struct kb_cands_uint3uint *result);
static bool decode_repeated_kb_cands_result_err_m(zcbor_state_t *state, struct kb_cands_result_err_m *result);
static bool decode_kb_cands(zcbor_state_t *state, struct kb_cands *result);
static bool decode_msg_kb_cands(zcbor_state_t *state, struct msg_kb_cands *result);
static bool decode_repeated_usage_uint1uint(zcbor_state_t *state, struct usage_uint1uint *result);
static bool decode_repeated_usage_uint2uint(zcbor_state_t *state, struct usage_uint2uint *result);
static bool decode_repeated_usage_uint3uint(zcbor_state_t *state, struct usage_uint3uint *result);
static bool decode_repeated_usage_uint4uint(zcbor_state_t *state, struct usage_uint4uint *result);
static bool decode_repeated_usage_uint5uint(zcbor_state_t *state, struct usage_uint5uint *result);
static bool decode_repeated_usage_uint6uint(zcbor_state_t *state, struct usage_uint6uint *result);
static bool decode_usage(zcbor_state_t *state, struct usage *result);
static bool decode_msg_usage(zcbor_state_t *state, struct msg_usage *result);
static bool decode_repeated_fw_begin_uint2tstr(zcbor_state_t *state, struct fw_begin_uint2tstr *result);
static bool decode_fw_begin(zcbor_state_t *state, struct fw_begin *result);
static bool decode_msg_fw_begin(zcbor_state_t *state, struct msg_fw_begin *result);
static bool decode_fw_chunk(zcbor_state_t *state, struct fw_chunk *result);
static bool decode_msg_fw_chunk(zcbor_state_t *state, struct msg_fw_chunk *result);
static bool decode_fw_status(zcbor_state_t *state, struct fw_status_r *result);
static bool decode_repeated_fw_ack_uint2tstr(zcbor_state_t *state, struct fw_ack_uint2tstr *result);
static bool decode_fw_ack(zcbor_state_t *state, struct fw_ack *result);
static bool decode_msg_fw_ack(zcbor_state_t *state, struct msg_fw_ack *result);
static bool decode_power(zcbor_state_t *state, struct power *result);
static bool decode_msg_power(zcbor_state_t *state, struct msg_power *result);
static bool decode_msg_dbg_ui_state_req(zcbor_state_t *state, struct msg_dbg_ui_state_req *result);
static bool decode_repeated_dbg_ui_state_uint1uint(zcbor_state_t *state, struct dbg_ui_state_uint1uint *result);
static bool decode_repeated_dbg_ui_state_uint2uint(zcbor_state_t *state, struct dbg_ui_state_uint2uint *result);
static bool decode_repeated_dbg_ui_state_uint5uint(zcbor_state_t *state, struct dbg_ui_state_uint5uint *result);
static bool decode_dbg_ui_state(zcbor_state_t *state, struct dbg_ui_state *result);
static bool decode_msg_dbg_ui_state(zcbor_state_t *state, struct msg_dbg_ui_state *result);
static bool decode_msg_dbg_screenshot_req(zcbor_state_t *state, struct msg_dbg_screenshot_req *result);
static bool decode_dbg_screenshot_chunk(zcbor_state_t *state, struct dbg_screenshot_chunk *result);
static bool decode_msg_dbg_screenshot_chunk(zcbor_state_t *state, struct msg_dbg_screenshot_chunk *result);
static bool decode_input_event(zcbor_state_t *state, struct input_event_r *result);
static bool decode_repeated_dbg_inject_input_uint1int(zcbor_state_t *state, struct dbg_inject_input_uint1int *result);
static bool decode_repeated_dbg_inject_input_uint2int(zcbor_state_t *state, struct dbg_inject_input_uint2int *result);
static bool decode_dbg_inject_input(zcbor_state_t *state, struct dbg_inject_input *result);
static bool decode_msg_dbg_inject_input(zcbor_state_t *state, struct msg_dbg_inject_input *result);
static bool decode_dbg_set_log_level(zcbor_state_t *state, struct dbg_set_log_level *result);
static bool decode_msg_dbg_set_log_level(zcbor_state_t *state, struct msg_dbg_set_log_level *result);
static bool decode_envelope(zcbor_state_t *state, struct envelope_r *result);


static bool decode_repeated_hdr_uint2uint(
		zcbor_state_t *state, struct hdr_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).hdr_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_hdr(
		zcbor_state_t *state, struct hdr_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).hdr_uint1uint))))
	&& zcbor_present_decode(&((*result).hdr_uint2uint_present), (zcbor_decoder_t *)decode_repeated_hdr_uint2uint, state, (&(*result).hdr_uint2uint)))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_hdr_uint2uint(state, (&(*result).hdr_uint2uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_device_class(
		zcbor_state_t *state, struct device_class_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).device_class_choice, sizeof((*result).device_class_choice)))) && ((((((*result).device_class_choice == device_class_watch_m_c) && ((1)))
	|| (((*result).device_class_choice == device_class_desk_m_c) && ((1)))
	|| (((*result).device_class_choice == device_class_pad_m_c) && ((1)))
	|| (((*result).device_class_choice == device_class_sim_m_c) && ((1)))
	|| (((*result).device_class_choice == device_class_other_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_device_info(
		zcbor_state_t *state, struct device_info *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (decode_device_class(state, (&(*result).device_info_device_class_m))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_tstr_decode(state, (&(*result).device_info_uint1tstr))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_tstr_decode(state, (&(*result).device_info_uint2tstr))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (zcbor_bstr_decode(state, (&(*result).device_info_uint3bstr)))
	&& ((((*result).device_info_uint3bstr.len == 8)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_disp_shape(
		zcbor_state_t *state, struct disp_shape_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).disp_shape_choice, sizeof((*result).disp_shape_choice)))) && ((((((*result).disp_shape_choice == disp_shape_round_m_c) && ((1)))
	|| (((*result).disp_shape_choice == disp_shape_rect_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_display_caps(
		zcbor_state_t *state, struct display_caps *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (decode_disp_shape(state, (&(*result).display_caps_disp_shape_m))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).display_caps_uint1uint))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).display_caps_uint2uint))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (zcbor_bool_decode(state, (&(*result).display_caps_uint3bool))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_display_caps_m(
		zcbor_state_t *state, struct display_caps_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (0))))
	&& (decode_display_caps(state, (&(*result).display_caps_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_input_kind(
		zcbor_state_t *state, struct input_kind_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).input_kind_choice, sizeof((*result).input_kind_choice)))) && ((((((*result).input_kind_choice == input_kind_touch_m_c) && ((1)))
	|| (((*result).input_kind_choice == input_kind_btn_m_c) && ((1)))
	|| (((*result).input_kind_choice == input_kind_rotary_m_c) && ((1)))
	|| (((*result).input_kind_choice == input_kind_imu_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_caps_uint2uint(
		zcbor_state_t *state, struct caps_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).caps_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_feature(
		zcbor_state_t *state, struct feature_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).feature_choice, sizeof((*result).feature_choice)))) && ((((((*result).feature_choice == feature_focus_m_c) && ((1)))
	|| (((*result).feature_choice == feature_ack_m_c) && ((1)))
	|| (((*result).feature_choice == feature_approve_m_c) && ((1)))
	|| (((*result).feature_choice == feature_reply_m_c) && ((1)))
	|| (((*result).feature_choice == feature_voice_m_c) && ((1)))
	|| (((*result).feature_choice == feature_badge_m_c) && ((1)))
	|| (((*result).feature_choice == feature_debug_m_c) && ((1)))
	|| (((*result).feature_choice == feature_scroll_m_c) && ((1)))
	|| (((*result).feature_choice == feature_keys_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_caps(
		zcbor_state_t *state, struct caps *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && ((zcbor_present_decode(&((*result).display_caps_m_present), (zcbor_decoder_t *)decode_repeated_display_caps_m, state, (&(*result).display_caps_m))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 4, &(*result).caps_input_kind_m_l_input_kind_m_count, (zcbor_decoder_t *)decode_input_kind, state, (*&(*result).caps_input_kind_m_l_input_kind_m), sizeof(struct input_kind_r))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& zcbor_present_decode(&((*result).caps_uint2uint_present), (zcbor_decoder_t *)decode_repeated_caps_uint2uint, state, (&(*result).caps_uint2uint))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (zcbor_bool_decode(state, (&(*result).caps_uint3bool))))
	&& (((zcbor_uint32_expect(state, (4))))
	&& (zcbor_bool_decode(state, (&(*result).caps_uint4bool))))
	&& (((zcbor_uint32_expect(state, (5))))
	&& (zcbor_bool_decode(state, (&(*result).caps_uint5bool))))
	&& (((zcbor_uint32_expect(state, (6))))
	&& (zcbor_bool_decode(state, (&(*result).caps_uint6bool))))
	&& (((zcbor_uint32_expect(state, (7))))
	&& (zcbor_uint32_decode(state, (&(*result).caps_uint7uint))))
	&& (((zcbor_uint32_expect(state, (8))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 12, &(*result).caps_feature_m_l_feature_m_count, (zcbor_decoder_t *)decode_feature, state, (*&(*result).caps_feature_m_l_feature_m), sizeof(struct feature_r))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_input_kind(state, (*&(*result).caps_input_kind_m_l_input_kind_m));
		decode_feature(state, (*&(*result).caps_feature_m_l_feature_m));
		decode_repeated_display_caps_m(state, (&(*result).display_caps_m));
		decode_repeated_caps_uint2uint(state, (&(*result).caps_uint2uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_hello_uint3uint(
		zcbor_state_t *state, struct hello_uint3uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_uint32_decode(state, (&(*result).hello_uint3uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_hello(
		zcbor_state_t *state, struct hello *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_list_start_decode(state) && ((((zcbor_uint32_decode(state, (&(*result).hello_major_l_major))))
	&& ((zcbor_uint32_decode(state, (&(*result).hello_major_l_minor))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (decode_device_info(state, (&(*result).hello_device_info_m))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (decode_caps(state, (&(*result).hello_caps_m))))
	&& zcbor_present_decode(&((*result).hello_uint3uint_present), (zcbor_decoder_t *)decode_repeated_hello_uint3uint, state, (&(*result).hello_uint3uint))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_hello_uint3uint(state, (&(*result).hello_uint3uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_hello(
		zcbor_state_t *state, struct msg_hello *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (1))))
	&& ((decode_hdr(state, (&(*result).msg_hello_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_hello(state, (&(*result).msg_hello_hello_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_host_os(
		zcbor_state_t *state, struct host_os_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).host_os_choice, sizeof((*result).host_os_choice)))) && ((((((*result).host_os_choice == host_os_mac_m_c) && ((1)))
	|| (((*result).host_os_choice == host_os_win_m_c) && ((1)))
	|| (((*result).host_os_choice == host_os_linux_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_host_info(
		zcbor_state_t *state, struct host_info *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_bstr_decode(state, (&(*result).host_info_uint0bstr)))
	&& ((((*result).host_info_uint0bstr.len == 8)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_tstr_decode(state, (&(*result).host_info_uint1tstr))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (decode_host_os(state, (&(*result).host_info_host_os_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_time_sync(
		zcbor_state_t *state, struct time_sync *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).time_sync_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_int32_decode(state, (&(*result).time_sync_uint1int))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_privacy(
		zcbor_state_t *state, struct privacy_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).privacy_choice, sizeof((*result).privacy_choice)))) && ((((((*result).privacy_choice == privacy_meta_m_c) && ((1)))
	|| (((*result).privacy_choice == privacy_preview_m_c) && ((1)))
	|| (((*result).privacy_choice == privacy_full_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_device_cfg_privacy_m(
		zcbor_state_t *state, struct device_cfg_privacy_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (0))))
	&& (decode_privacy(state, (&(*result).device_cfg_privacy_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_alert_mode(
		zcbor_state_t *state, struct alert_mode_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).alert_mode_choice, sizeof((*result).alert_mode_choice)))) && ((((((*result).alert_mode_choice == alert_mode_full_m_c) && ((1)))
	|| (((*result).alert_mode_choice == alert_mode_quiet_m_c) && ((1)))
	|| (((*result).alert_mode_choice == alert_mode_silent_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_device_cfg_alert_mode_m(
		zcbor_state_t *state, struct device_cfg_alert_mode_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (decode_alert_mode(state, (&(*result).device_cfg_alert_mode_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_device_cfg_uint2uint(
		zcbor_state_t *state, struct device_cfg_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).device_cfg_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_device_cfg_uint3uint(
		zcbor_state_t *state, struct device_cfg_uint3uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_uint32_decode(state, (&(*result).device_cfg_uint3uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_device_cfg_uint4tstr(
		zcbor_state_t *state, struct device_cfg_uint4tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (zcbor_tstr_decode(state, (&(*result).device_cfg_uint4tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_device_cfg(
		zcbor_state_t *state, struct device_cfg *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && ((zcbor_present_decode(&((*result).device_cfg_privacy_m_present), (zcbor_decoder_t *)decode_repeated_device_cfg_privacy_m, state, (&(*result).device_cfg_privacy_m))
	&& zcbor_present_decode(&((*result).device_cfg_alert_mode_m_present), (zcbor_decoder_t *)decode_repeated_device_cfg_alert_mode_m, state, (&(*result).device_cfg_alert_mode_m))
	&& zcbor_present_decode(&((*result).device_cfg_uint2uint_present), (zcbor_decoder_t *)decode_repeated_device_cfg_uint2uint, state, (&(*result).device_cfg_uint2uint))
	&& zcbor_present_decode(&((*result).device_cfg_uint3uint_present), (zcbor_decoder_t *)decode_repeated_device_cfg_uint3uint, state, (&(*result).device_cfg_uint3uint))
	&& zcbor_present_decode(&((*result).device_cfg_uint4tstr_present), (zcbor_decoder_t *)decode_repeated_device_cfg_uint4tstr, state, (&(*result).device_cfg_uint4tstr))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_device_cfg_privacy_m(state, (&(*result).device_cfg_privacy_m));
		decode_repeated_device_cfg_alert_mode_m(state, (&(*result).device_cfg_alert_mode_m));
		decode_repeated_device_cfg_uint2uint(state, (&(*result).device_cfg_uint2uint));
		decode_repeated_device_cfg_uint3uint(state, (&(*result).device_cfg_uint3uint));
		decode_repeated_device_cfg_uint4tstr(state, (&(*result).device_cfg_uint4tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_welcome_tstr_l(
		zcbor_state_t *state, struct welcome_tstr_l_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 6, &(*result).welcome_tstr_l_tstr_count, (zcbor_decoder_t *)zcbor_tstr_decode, state, (*&(*result).welcome_tstr_l_tstr), sizeof(struct zcbor_string))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_tstr_decode(state, (*&(*result).welcome_tstr_l_tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_welcome_uint5bool(
		zcbor_state_t *state, struct welcome_uint5bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (5))))
	&& (zcbor_bool_decode(state, (&(*result).welcome_uint5bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_welcome_uint6bool(
		zcbor_state_t *state, struct welcome_uint6bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (6))))
	&& (zcbor_bool_decode(state, (&(*result).welcome_uint6bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_welcome_uint7bool(
		zcbor_state_t *state, struct welcome_uint7bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (7))))
	&& (zcbor_bool_decode(state, (&(*result).welcome_uint7bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_welcome(
		zcbor_state_t *state, struct welcome *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_list_start_decode(state) && ((((zcbor_uint32_decode(state, (&(*result).welcome_major_l_major))))
	&& ((zcbor_uint32_decode(state, (&(*result).welcome_major_l_minor))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (decode_host_info(state, (&(*result).welcome_host_info_m))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (decode_time_sync(state, (&(*result).welcome_time_sync_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_device_cfg(state, (&(*result).welcome_device_cfg_m))))
	&& zcbor_present_decode(&((*result).welcome_tstr_l_present), (zcbor_decoder_t *)decode_repeated_welcome_tstr_l, state, (&(*result).welcome_tstr_l))
	&& zcbor_present_decode(&((*result).welcome_uint5bool_present), (zcbor_decoder_t *)decode_repeated_welcome_uint5bool, state, (&(*result).welcome_uint5bool))
	&& zcbor_present_decode(&((*result).welcome_uint6bool_present), (zcbor_decoder_t *)decode_repeated_welcome_uint6bool, state, (&(*result).welcome_uint6bool))
	&& zcbor_present_decode(&((*result).welcome_uint7bool_present), (zcbor_decoder_t *)decode_repeated_welcome_uint7bool, state, (&(*result).welcome_uint7bool))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_welcome_tstr_l(state, (&(*result).welcome_tstr_l));
		decode_repeated_welcome_uint5bool(state, (&(*result).welcome_uint5bool));
		decode_repeated_welcome_uint6bool(state, (&(*result).welcome_uint6bool));
		decode_repeated_welcome_uint7bool(state, (&(*result).welcome_uint7bool));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_welcome(
		zcbor_state_t *state, struct msg_welcome *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (2))))
	&& ((decode_hdr(state, (&(*result).msg_welcome_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_welcome(state, (&(*result).msg_welcome_welcome_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_agent(
		zcbor_state_t *state, struct agent_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).agent_choice, sizeof((*result).agent_choice)))) && ((((((*result).agent_choice == agent_copilot_m_c) && ((1)))
	|| (((*result).agent_choice == agent_claude_m_c) && ((1)))
	|| (((*result).agent_choice == agent_cloud_m_c) && ((1)))
	|| (((*result).agent_choice == agent_shell_m_c) && ((1)))
	|| (((*result).agent_choice == agent_other_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_agent_m(
		zcbor_state_t *state, struct session_agent_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (decode_agent(state, (&(*result).session_agent_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_sess_state(
		zcbor_state_t *state, struct sess_state_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).sess_state_choice, sizeof((*result).sess_state_choice)))) && ((((((*result).sess_state_choice == sess_state_start_m_c) && ((1)))
	|| (((*result).sess_state_choice == sess_state_work_m_c) && ((1)))
	|| (((*result).sess_state_choice == sess_state_input_m_c) && ((1)))
	|| (((*result).sess_state_choice == sess_state_error_m_c) && ((1)))
	|| (((*result).sess_state_choice == sess_state_done_m_c) && ((1)))
	|| (((*result).sess_state_choice == sess_state_idle_m_c) && ((1)))
	|| (((*result).sess_state_choice == sess_state_ended_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_sess_state_m(
		zcbor_state_t *state, struct session_sess_state_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (decode_sess_state(state, (&(*result).session_sess_state_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_input_sub(
		zcbor_state_t *state, struct input_sub_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).input_sub_choice, sizeof((*result).input_sub_choice)))) && ((((((*result).input_sub_choice == input_sub_approval_m_c) && ((1)))
	|| (((*result).input_sub_choice == input_sub_question_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_input_sub_m(
		zcbor_state_t *state, struct session_input_sub_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (decode_input_sub(state, (&(*result).session_input_sub_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint4tstr(
		zcbor_state_t *state, struct session_uint4tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint4tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint5tstr(
		zcbor_state_t *state, struct session_uint5tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (5))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint5tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint6tstr(
		zcbor_state_t *state, struct session_uint6tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (6))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint6tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint7uint(
		zcbor_state_t *state, struct session_uint7uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (7))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint7uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint8uint(
		zcbor_state_t *state, struct session_uint8uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (8))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint8uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint9tstr(
		zcbor_state_t *state, struct session_uint9tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (9))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint9tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint10bool(
		zcbor_state_t *state, struct session_uint10bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (10))))
	&& (zcbor_bool_decode(state, (&(*result).session_uint10bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint11uint(
		zcbor_state_t *state, struct session_uint11uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (11))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint11uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint12tstr(
		zcbor_state_t *state, struct session_uint12tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (12))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint12tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint13uint(
		zcbor_state_t *state, struct session_uint13uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (13))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint13uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint14bool(
		zcbor_state_t *state, struct session_uint14bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (14))))
	&& (zcbor_bool_decode(state, (&(*result).session_uint14bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint15uint(
		zcbor_state_t *state, struct session_uint15uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (15))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint15uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint16bool(
		zcbor_state_t *state, struct session_uint16bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (16))))
	&& (zcbor_bool_decode(state, (&(*result).session_uint16bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint17tstr(
		zcbor_state_t *state, struct session_uint17tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (17))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint17tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_tstr_l(
		zcbor_state_t *state, struct session_tstr_l_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (18))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 3, &(*result).session_tstr_l_tstr_count, (zcbor_decoder_t *)zcbor_tstr_decode, state, (*&(*result).session_tstr_l_tstr), sizeof(struct zcbor_string))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_tstr_decode(state, (*&(*result).session_tstr_l_tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint19uint(
		zcbor_state_t *state, struct session_uint19uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (19))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint19uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint20uint(
		zcbor_state_t *state, struct session_uint20uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (20))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint20uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint21uint(
		zcbor_state_t *state, struct session_uint21uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (21))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint21uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint22tstr(
		zcbor_state_t *state, struct session_uint22tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (22))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint22tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_perm_mode(
		zcbor_state_t *state, struct perm_mode_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).perm_mode_choice, sizeof((*result).perm_mode_choice)))) && ((((((*result).perm_mode_choice == perm_mode_default_m_c) && ((1)))
	|| (((*result).perm_mode_choice == perm_mode_plan_m_c) && ((1)))
	|| (((*result).perm_mode_choice == perm_mode_accept_edits_m_c) && ((1)))
	|| (((*result).perm_mode_choice == perm_mode_auto_m_c) && ((1)))
	|| (((*result).perm_mode_choice == perm_mode_dont_ask_m_c) && ((1)))
	|| (((*result).perm_mode_choice == perm_mode_bypass_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_perm_mode_m(
		zcbor_state_t *state, struct session_perm_mode_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (23))))
	&& (decode_perm_mode(state, (&(*result).session_perm_mode_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint24uint(
		zcbor_state_t *state, struct session_uint24uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (24))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint24uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_pr_state(
		zcbor_state_t *state, struct pr_state_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).pr_state_choice, sizeof((*result).pr_state_choice)))) && ((((((*result).pr_state_choice == pr_state_none_m_c) && ((1)))
	|| (((*result).pr_state_choice == pr_state_pending_m_c) && ((1)))
	|| (((*result).pr_state_choice == pr_state_approved_m_c) && ((1)))
	|| (((*result).pr_state_choice == pr_state_changes_m_c) && ((1)))
	|| (((*result).pr_state_choice == pr_state_draft_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_pr_state_m(
		zcbor_state_t *state, struct session_pr_state_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (25))))
	&& (decode_pr_state(state, (&(*result).session_pr_state_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint26bool(
		zcbor_state_t *state, struct session_uint26bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (26))))
	&& (zcbor_bool_decode(state, (&(*result).session_uint26bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint30uint(
		zcbor_state_t *state, struct session_uint30uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (30))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint30uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint31uint(
		zcbor_state_t *state, struct session_uint31uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (31))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint31uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint32uint(
		zcbor_state_t *state, struct session_uint32uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (32))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint32uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint33bool(
		zcbor_state_t *state, struct session_uint33bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (33))))
	&& (zcbor_bool_decode(state, (&(*result).session_uint33bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint34uint(
		zcbor_state_t *state, struct session_uint34uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (34))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint34uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint35tstr(
		zcbor_state_t *state, struct session_uint35tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (35))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint35tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint36uint(
		zcbor_state_t *state, struct session_uint36uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (36))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint36uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint37uint(
		zcbor_state_t *state, struct session_uint37uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (37))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint37uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_session_uint38tstr(
		zcbor_state_t *state, struct session_uint38tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (38))))
	&& (zcbor_tstr_decode(state, (&(*result).session_uint38tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_session(
		zcbor_state_t *state, struct session *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).session_uint0uint))))
	&& zcbor_present_decode(&((*result).session_agent_m_present), (zcbor_decoder_t *)decode_repeated_session_agent_m, state, (&(*result).session_agent_m))
	&& zcbor_present_decode(&((*result).session_sess_state_m_present), (zcbor_decoder_t *)decode_repeated_session_sess_state_m, state, (&(*result).session_sess_state_m))
	&& zcbor_present_decode(&((*result).session_input_sub_m_present), (zcbor_decoder_t *)decode_repeated_session_input_sub_m, state, (&(*result).session_input_sub_m))
	&& zcbor_present_decode(&((*result).session_uint4tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint4tstr, state, (&(*result).session_uint4tstr))
	&& zcbor_present_decode(&((*result).session_uint5tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint5tstr, state, (&(*result).session_uint5tstr))
	&& zcbor_present_decode(&((*result).session_uint6tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint6tstr, state, (&(*result).session_uint6tstr))
	&& zcbor_present_decode(&((*result).session_uint7uint_present), (zcbor_decoder_t *)decode_repeated_session_uint7uint, state, (&(*result).session_uint7uint))
	&& zcbor_present_decode(&((*result).session_uint8uint_present), (zcbor_decoder_t *)decode_repeated_session_uint8uint, state, (&(*result).session_uint8uint))
	&& zcbor_present_decode(&((*result).session_uint9tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint9tstr, state, (&(*result).session_uint9tstr))
	&& zcbor_present_decode(&((*result).session_uint10bool_present), (zcbor_decoder_t *)decode_repeated_session_uint10bool, state, (&(*result).session_uint10bool))
	&& zcbor_present_decode(&((*result).session_uint11uint_present), (zcbor_decoder_t *)decode_repeated_session_uint11uint, state, (&(*result).session_uint11uint))
	&& zcbor_present_decode(&((*result).session_uint12tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint12tstr, state, (&(*result).session_uint12tstr))
	&& zcbor_present_decode(&((*result).session_uint13uint_present), (zcbor_decoder_t *)decode_repeated_session_uint13uint, state, (&(*result).session_uint13uint))
	&& zcbor_present_decode(&((*result).session_uint14bool_present), (zcbor_decoder_t *)decode_repeated_session_uint14bool, state, (&(*result).session_uint14bool))
	&& zcbor_present_decode(&((*result).session_uint15uint_present), (zcbor_decoder_t *)decode_repeated_session_uint15uint, state, (&(*result).session_uint15uint))
	&& zcbor_present_decode(&((*result).session_uint16bool_present), (zcbor_decoder_t *)decode_repeated_session_uint16bool, state, (&(*result).session_uint16bool))
	&& zcbor_present_decode(&((*result).session_uint17tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint17tstr, state, (&(*result).session_uint17tstr))
	&& zcbor_present_decode(&((*result).session_tstr_l_present), (zcbor_decoder_t *)decode_repeated_session_tstr_l, state, (&(*result).session_tstr_l))
	&& zcbor_present_decode(&((*result).session_uint19uint_present), (zcbor_decoder_t *)decode_repeated_session_uint19uint, state, (&(*result).session_uint19uint))
	&& zcbor_present_decode(&((*result).session_uint20uint_present), (zcbor_decoder_t *)decode_repeated_session_uint20uint, state, (&(*result).session_uint20uint))
	&& zcbor_present_decode(&((*result).session_uint21uint_present), (zcbor_decoder_t *)decode_repeated_session_uint21uint, state, (&(*result).session_uint21uint))
	&& zcbor_present_decode(&((*result).session_uint22tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint22tstr, state, (&(*result).session_uint22tstr))
	&& zcbor_present_decode(&((*result).session_perm_mode_m_present), (zcbor_decoder_t *)decode_repeated_session_perm_mode_m, state, (&(*result).session_perm_mode_m))
	&& zcbor_present_decode(&((*result).session_uint24uint_present), (zcbor_decoder_t *)decode_repeated_session_uint24uint, state, (&(*result).session_uint24uint))
	&& zcbor_present_decode(&((*result).session_pr_state_m_present), (zcbor_decoder_t *)decode_repeated_session_pr_state_m, state, (&(*result).session_pr_state_m))
	&& zcbor_present_decode(&((*result).session_uint26bool_present), (zcbor_decoder_t *)decode_repeated_session_uint26bool, state, (&(*result).session_uint26bool))
	&& zcbor_present_decode(&((*result).session_uint30uint_present), (zcbor_decoder_t *)decode_repeated_session_uint30uint, state, (&(*result).session_uint30uint))
	&& zcbor_present_decode(&((*result).session_uint31uint_present), (zcbor_decoder_t *)decode_repeated_session_uint31uint, state, (&(*result).session_uint31uint))
	&& zcbor_present_decode(&((*result).session_uint32uint_present), (zcbor_decoder_t *)decode_repeated_session_uint32uint, state, (&(*result).session_uint32uint))
	&& zcbor_present_decode(&((*result).session_uint33bool_present), (zcbor_decoder_t *)decode_repeated_session_uint33bool, state, (&(*result).session_uint33bool))
	&& zcbor_present_decode(&((*result).session_uint34uint_present), (zcbor_decoder_t *)decode_repeated_session_uint34uint, state, (&(*result).session_uint34uint))
	&& zcbor_present_decode(&((*result).session_uint35tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint35tstr, state, (&(*result).session_uint35tstr))
	&& zcbor_present_decode(&((*result).session_uint36uint_present), (zcbor_decoder_t *)decode_repeated_session_uint36uint, state, (&(*result).session_uint36uint))
	&& zcbor_present_decode(&((*result).session_uint37uint_present), (zcbor_decoder_t *)decode_repeated_session_uint37uint, state, (&(*result).session_uint37uint))
	&& zcbor_present_decode(&((*result).session_uint38tstr_present), (zcbor_decoder_t *)decode_repeated_session_uint38tstr, state, (&(*result).session_uint38tstr))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_session_agent_m(state, (&(*result).session_agent_m));
		decode_repeated_session_sess_state_m(state, (&(*result).session_sess_state_m));
		decode_repeated_session_input_sub_m(state, (&(*result).session_input_sub_m));
		decode_repeated_session_uint4tstr(state, (&(*result).session_uint4tstr));
		decode_repeated_session_uint5tstr(state, (&(*result).session_uint5tstr));
		decode_repeated_session_uint6tstr(state, (&(*result).session_uint6tstr));
		decode_repeated_session_uint7uint(state, (&(*result).session_uint7uint));
		decode_repeated_session_uint8uint(state, (&(*result).session_uint8uint));
		decode_repeated_session_uint9tstr(state, (&(*result).session_uint9tstr));
		decode_repeated_session_uint10bool(state, (&(*result).session_uint10bool));
		decode_repeated_session_uint11uint(state, (&(*result).session_uint11uint));
		decode_repeated_session_uint12tstr(state, (&(*result).session_uint12tstr));
		decode_repeated_session_uint13uint(state, (&(*result).session_uint13uint));
		decode_repeated_session_uint14bool(state, (&(*result).session_uint14bool));
		decode_repeated_session_uint15uint(state, (&(*result).session_uint15uint));
		decode_repeated_session_uint16bool(state, (&(*result).session_uint16bool));
		decode_repeated_session_uint17tstr(state, (&(*result).session_uint17tstr));
		decode_repeated_session_tstr_l(state, (&(*result).session_tstr_l));
		decode_repeated_session_uint19uint(state, (&(*result).session_uint19uint));
		decode_repeated_session_uint20uint(state, (&(*result).session_uint20uint));
		decode_repeated_session_uint21uint(state, (&(*result).session_uint21uint));
		decode_repeated_session_uint22tstr(state, (&(*result).session_uint22tstr));
		decode_repeated_session_perm_mode_m(state, (&(*result).session_perm_mode_m));
		decode_repeated_session_uint24uint(state, (&(*result).session_uint24uint));
		decode_repeated_session_pr_state_m(state, (&(*result).session_pr_state_m));
		decode_repeated_session_uint26bool(state, (&(*result).session_uint26bool));
		decode_repeated_session_uint30uint(state, (&(*result).session_uint30uint));
		decode_repeated_session_uint31uint(state, (&(*result).session_uint31uint));
		decode_repeated_session_uint32uint(state, (&(*result).session_uint32uint));
		decode_repeated_session_uint33bool(state, (&(*result).session_uint33bool));
		decode_repeated_session_uint34uint(state, (&(*result).session_uint34uint));
		decode_repeated_session_uint35tstr(state, (&(*result).session_uint35tstr));
		decode_repeated_session_uint36uint(state, (&(*result).session_uint36uint));
		decode_repeated_session_uint37uint(state, (&(*result).session_uint37uint));
		decode_repeated_session_uint38tstr(state, (&(*result).session_uint38tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_att_kind(
		zcbor_state_t *state, struct att_kind_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).att_kind_choice, sizeof((*result).att_kind_choice)))) && ((((((*result).att_kind_choice == att_kind_approval_m_c) && ((1)))
	|| (((*result).att_kind_choice == att_kind_question_m_c) && ((1)))
	|| (((*result).att_kind_choice == att_kind_error_m_c) && ((1)))
	|| (((*result).att_kind_choice == att_kind_done_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_away_event(
		zcbor_state_t *state, struct away_event *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).away_event_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (decode_att_kind(state, (&(*result).away_event_att_kind_m))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).away_event_uint2uint))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_snapshot_away_event_m_l(
		zcbor_state_t *state, struct snapshot_away_event_m_l_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 16, &(*result).snapshot_away_event_m_l_away_event_m_count, (zcbor_decoder_t *)decode_away_event, state, (*&(*result).snapshot_away_event_m_l_away_event_m), sizeof(struct away_event))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_away_event(state, (*&(*result).snapshot_away_event_m_l_away_event_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_snapshot(
		zcbor_state_t *state, struct snapshot *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 32, &(*result).snapshot_session_m_l_session_m_count, (zcbor_decoder_t *)decode_session, state, (*&(*result).snapshot_session_m_l_session_m), sizeof(struct session))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& zcbor_present_decode(&((*result).snapshot_away_event_m_l_present), (zcbor_decoder_t *)decode_repeated_snapshot_away_event_m_l, state, (&(*result).snapshot_away_event_m_l))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_session(state, (*&(*result).snapshot_session_m_l_session_m));
		decode_repeated_snapshot_away_event_m_l(state, (&(*result).snapshot_away_event_m_l));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_snapshot(
		zcbor_state_t *state, struct msg_snapshot *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (3))))
	&& ((decode_hdr(state, (&(*result).msg_snapshot_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_snapshot(state, (&(*result).msg_snapshot_snapshot_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_session_upd(
		zcbor_state_t *state, struct msg_session_upd *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (4))))
	&& ((decode_hdr(state, (&(*result).msg_session_upd_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_session(state, (&(*result).msg_session_upd_session_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_gone(
		zcbor_state_t *state, struct gone *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).gone_uint0uint))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_gone(
		zcbor_state_t *state, struct msg_gone *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (5))))
	&& ((decode_hdr(state, (&(*result).msg_gone_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_gone(state, (&(*result).msg_gone_gone_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint3tstr(
		zcbor_state_t *state, struct attention_uint3tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_tstr_decode(state, (&(*result).attention_uint3tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint4tstr(
		zcbor_state_t *state, struct attention_uint4tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (zcbor_tstr_decode(state, (&(*result).attention_uint4tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_tstr_l(
		zcbor_state_t *state, struct attention_tstr_l_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (5))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 4, &(*result).attention_tstr_l_tstr_count, (zcbor_decoder_t *)zcbor_tstr_decode, state, (*&(*result).attention_tstr_l_tstr), sizeof(struct zcbor_string))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_tstr_decode(state, (*&(*result).attention_tstr_l_tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_action(
		zcbor_state_t *state, struct action_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).action_choice, sizeof((*result).action_choice)))) && ((((((*result).action_choice == action_approve_m_c) && ((1)))
	|| (((*result).action_choice == action_deny_m_c) && ((1)))
	|| (((*result).action_choice == action_focus_m_c) && ((1)))
	|| (((*result).action_choice == action_reply_m_c) && ((1)))
	|| (((*result).action_choice == action_ack_m_c) && ((1)))
	|| (((*result).action_choice == action_dictate_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint7uint(
		zcbor_state_t *state, struct attention_uint7uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (7))))
	&& (zcbor_uint32_decode(state, (&(*result).attention_uint7uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint8uint(
		zcbor_state_t *state, struct attention_uint8uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (8))))
	&& (zcbor_uint32_decode(state, (&(*result).attention_uint8uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint9uint(
		zcbor_state_t *state, struct attention_uint9uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (9))))
	&& (zcbor_uint32_decode(state, (&(*result).attention_uint9uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint10uint(
		zcbor_state_t *state, struct attention_uint10uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (10))))
	&& (zcbor_uint32_decode(state, (&(*result).attention_uint10uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint11uint(
		zcbor_state_t *state, struct attention_uint11uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (11))))
	&& (zcbor_uint32_decode(state, (&(*result).attention_uint11uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint12bool(
		zcbor_state_t *state, struct attention_uint12bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (12))))
	&& (zcbor_bool_decode(state, (&(*result).attention_uint12bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint13tstr(
		zcbor_state_t *state, struct attention_uint13tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (13))))
	&& (zcbor_tstr_decode(state, (&(*result).attention_uint13tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint14uint(
		zcbor_state_t *state, struct attention_uint14uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (14))))
	&& (zcbor_uint32_decode(state, (&(*result).attention_uint14uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_uint15bool(
		zcbor_state_t *state, struct attention_uint15bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (15))))
	&& (zcbor_bool_decode(state, (&(*result).attention_uint15bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_ask_opt_uint1tstr(
		zcbor_state_t *state, struct ask_opt_uint1tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_tstr_decode(state, (&(*result).ask_opt_uint1tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_ask_opt(
		zcbor_state_t *state, struct ask_opt *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_tstr_decode(state, (&(*result).ask_opt_uint0tstr))))
	&& zcbor_present_decode(&((*result).ask_opt_uint1tstr_present), (zcbor_decoder_t *)decode_repeated_ask_opt_uint1tstr, state, (&(*result).ask_opt_uint1tstr))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_ask_opt_uint1tstr(state, (&(*result).ask_opt_uint1tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_ask_q_uint3bool(
		zcbor_state_t *state, struct ask_q_uint3bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_bool_decode(state, (&(*result).ask_q_uint3bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_ask_q(
		zcbor_state_t *state, struct ask_q *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_tstr_decode(state, (&(*result).ask_q_uint0tstr))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_tstr_decode(state, (&(*result).ask_q_uint1tstr))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 4, &(*result).ask_q_ask_opt_m_l_ask_opt_m_count, (zcbor_decoder_t *)decode_ask_opt, state, (*&(*result).ask_q_ask_opt_m_l_ask_opt_m), sizeof(struct ask_opt))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& zcbor_present_decode(&((*result).ask_q_uint3bool_present), (zcbor_decoder_t *)decode_repeated_ask_q_uint3bool, state, (&(*result).ask_q_uint3bool))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_ask_opt(state, (*&(*result).ask_q_ask_opt_m_l_ask_opt_m));
		decode_repeated_ask_q_uint3bool(state, (&(*result).ask_q_uint3bool));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_attention_ask_q_m_l(
		zcbor_state_t *state, struct attention_ask_q_m_l_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (16))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 4, &(*result).attention_ask_q_m_l_ask_q_m_count, (zcbor_decoder_t *)decode_ask_q, state, (*&(*result).attention_ask_q_m_l_ask_q_m), sizeof(struct ask_q))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_ask_q(state, (*&(*result).attention_ask_q_m_l_ask_q_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_attention(
		zcbor_state_t *state, struct attention *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).attention_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).attention_uint1uint))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (decode_att_kind(state, (&(*result).attention_att_kind_m))))
	&& zcbor_present_decode(&((*result).attention_uint3tstr_present), (zcbor_decoder_t *)decode_repeated_attention_uint3tstr, state, (&(*result).attention_uint3tstr))
	&& zcbor_present_decode(&((*result).attention_uint4tstr_present), (zcbor_decoder_t *)decode_repeated_attention_uint4tstr, state, (&(*result).attention_uint4tstr))
	&& zcbor_present_decode(&((*result).attention_tstr_l_present), (zcbor_decoder_t *)decode_repeated_attention_tstr_l, state, (&(*result).attention_tstr_l))
	&& (((zcbor_uint32_expect(state, (6))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 6, &(*result).attention_action_m_l_action_m_count, (zcbor_decoder_t *)decode_action, state, (*&(*result).attention_action_m_l_action_m), sizeof(struct action_r))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& zcbor_present_decode(&((*result).attention_uint7uint_present), (zcbor_decoder_t *)decode_repeated_attention_uint7uint, state, (&(*result).attention_uint7uint))
	&& zcbor_present_decode(&((*result).attention_uint8uint_present), (zcbor_decoder_t *)decode_repeated_attention_uint8uint, state, (&(*result).attention_uint8uint))
	&& zcbor_present_decode(&((*result).attention_uint9uint_present), (zcbor_decoder_t *)decode_repeated_attention_uint9uint, state, (&(*result).attention_uint9uint))
	&& zcbor_present_decode(&((*result).attention_uint10uint_present), (zcbor_decoder_t *)decode_repeated_attention_uint10uint, state, (&(*result).attention_uint10uint))
	&& zcbor_present_decode(&((*result).attention_uint11uint_present), (zcbor_decoder_t *)decode_repeated_attention_uint11uint, state, (&(*result).attention_uint11uint))
	&& zcbor_present_decode(&((*result).attention_uint12bool_present), (zcbor_decoder_t *)decode_repeated_attention_uint12bool, state, (&(*result).attention_uint12bool))
	&& zcbor_present_decode(&((*result).attention_uint13tstr_present), (zcbor_decoder_t *)decode_repeated_attention_uint13tstr, state, (&(*result).attention_uint13tstr))
	&& zcbor_present_decode(&((*result).attention_uint14uint_present), (zcbor_decoder_t *)decode_repeated_attention_uint14uint, state, (&(*result).attention_uint14uint))
	&& zcbor_present_decode(&((*result).attention_uint15bool_present), (zcbor_decoder_t *)decode_repeated_attention_uint15bool, state, (&(*result).attention_uint15bool))
	&& zcbor_present_decode(&((*result).attention_ask_q_m_l_present), (zcbor_decoder_t *)decode_repeated_attention_ask_q_m_l, state, (&(*result).attention_ask_q_m_l))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_action(state, (*&(*result).attention_action_m_l_action_m));
		decode_repeated_attention_uint3tstr(state, (&(*result).attention_uint3tstr));
		decode_repeated_attention_uint4tstr(state, (&(*result).attention_uint4tstr));
		decode_repeated_attention_tstr_l(state, (&(*result).attention_tstr_l));
		decode_repeated_attention_uint7uint(state, (&(*result).attention_uint7uint));
		decode_repeated_attention_uint8uint(state, (&(*result).attention_uint8uint));
		decode_repeated_attention_uint9uint(state, (&(*result).attention_uint9uint));
		decode_repeated_attention_uint10uint(state, (&(*result).attention_uint10uint));
		decode_repeated_attention_uint11uint(state, (&(*result).attention_uint11uint));
		decode_repeated_attention_uint12bool(state, (&(*result).attention_uint12bool));
		decode_repeated_attention_uint13tstr(state, (&(*result).attention_uint13tstr));
		decode_repeated_attention_uint14uint(state, (&(*result).attention_uint14uint));
		decode_repeated_attention_uint15bool(state, (&(*result).attention_uint15bool));
		decode_repeated_attention_ask_q_m_l(state, (&(*result).attention_ask_q_m_l));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_attention(
		zcbor_state_t *state, struct msg_attention *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (6))))
	&& ((decode_hdr(state, (&(*result).msg_attention_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_attention(state, (&(*result).msg_attention_attention_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_clear_why(
		zcbor_state_t *state, struct clear_why_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).clear_why_choice, sizeof((*result).clear_why_choice)))) && ((((((*result).clear_why_choice == clear_why_answered_pc_m_c) && ((1)))
	|| (((*result).clear_why_choice == clear_why_answered_dev_m_c) && ((1)))
	|| (((*result).clear_why_choice == clear_why_expired_m_c) && ((1)))
	|| (((*result).clear_why_choice == clear_why_cancelled_m_c) && ((1)))
	|| (((*result).clear_why_choice == clear_why_handed_back_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_clear(
		zcbor_state_t *state, struct clear *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).clear_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (decode_clear_why(state, (&(*result).clear_why_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_clear(
		zcbor_state_t *state, struct msg_clear *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (7))))
	&& ((decode_hdr(state, (&(*result).msg_clear_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_clear(state, (&(*result).msg_clear_clear_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_cmd_op(
		zcbor_state_t *state, struct cmd_op_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).cmd_op_choice, sizeof((*result).cmd_op_choice)))) && ((((((*result).cmd_op_choice == cmd_op_focus_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_ack_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_snooze_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_approve_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_deny_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_reply_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_cancel_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_detail_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_notify_done_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_quiet_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_scroll_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_type_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_dictate_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_kb_pick_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_kb_del_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_kb_send_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_kb_end_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_rerun_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_unqueue_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_answer_m_c) && ((1)))
	|| (((*result).cmd_op_choice == cmd_op_pointer_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_uint1uint(
		zcbor_state_t *state, struct cmd_uint1uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).cmd_uint1uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_uint2uint(
		zcbor_state_t *state, struct cmd_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).cmd_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_scroll_unit(
		zcbor_state_t *state, struct scroll_unit_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).scroll_unit_choice, sizeof((*result).scroll_unit_choice)))) && ((((((*result).scroll_unit_choice == scroll_unit_scroll_lines_m_c) && ((1)))
	|| (((*result).scroll_unit_choice == scroll_unit_scroll_pages_m_c) && ((1)))
	|| (((*result).scroll_unit_choice == scroll_unit_scroll_to_end_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_scroll_arg_uint2uint(
		zcbor_state_t *state, struct scroll_arg_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).scroll_arg_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_scroll_arg(
		zcbor_state_t *state, struct scroll_arg *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (decode_scroll_unit(state, (&(*result).scroll_arg_scroll_unit_m))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_int32_decode(state, (&(*result).scroll_arg_uint1int))))
	&& zcbor_present_decode(&((*result).scroll_arg_uint2uint_present), (zcbor_decoder_t *)decode_repeated_scroll_arg_uint2uint, state, (&(*result).scroll_arg_uint2uint))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_scroll_arg_uint2uint(state, (&(*result).scroll_arg_uint2uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_cmd_arg(
		zcbor_state_t *state, struct cmd_arg_r *result)
{
	zcbor_log("%s\r\n", __func__);
	bool int_res;

	bool res = (((zcbor_union_start_code(state) && (int_res = ((((zcbor_uint32_decode(state, (&(*result).cmd_arg_uint)))) && (((*result).cmd_arg_choice = cmd_arg_uint_c), true))
	|| (((zcbor_tstr_decode(state, (&(*result).cmd_arg_tstr)))) && (((*result).cmd_arg_choice = cmd_arg_tstr_c), true))
	|| (((decode_scroll_arg(state, (&(*result).cmd_arg_scroll_arg_m)))) && (((*result).cmd_arg_choice = cmd_arg_scroll_arg_m_c), true))), zcbor_union_end_code(state), int_res))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_arg_m(
		zcbor_state_t *state, struct cmd_arg_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (decode_cmd_arg(state, (&(*result).cmd_arg_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_uint4uint(
		zcbor_state_t *state, struct cmd_uint4uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (zcbor_uint32_decode(state, (&(*result).cmd_uint4uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_uint5uint(
		zcbor_state_t *state, struct cmd_uint5uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (5))))
	&& (zcbor_uint32_decode(state, (&(*result).cmd_uint5uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_uint6bool(
		zcbor_state_t *state, struct cmd_uint6bool *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (6))))
	&& (zcbor_bool_decode(state, (&(*result).cmd_uint6bool)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_answer_picks(
		zcbor_state_t *state, struct answer_picks *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 4, &(*result).answer_picks_picks_count, (zcbor_decoder_t *)zcbor_uint32_decode, state, (*&(*result).answer_picks_picks), sizeof(uint32_t))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_uint32_decode(state, (*&(*result).answer_picks_picks));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_answer_picks_m(
		zcbor_state_t *state, struct cmd_answer_picks_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (7))))
	&& (decode_answer_picks(state, (&(*result).cmd_answer_picks_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_answer_aids(
		zcbor_state_t *state, struct answer_aids *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 4, &(*result).answer_aids_aids_count, (zcbor_decoder_t *)zcbor_uint32_decode, state, (*&(*result).answer_aids_aids), sizeof(uint32_t))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_uint32_decode(state, (*&(*result).answer_aids_aids));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_answer_aids_m(
		zcbor_state_t *state, struct cmd_answer_aids_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (8))))
	&& (decode_answer_aids(state, (&(*result).cmd_answer_aids_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_pointer_arg_uint0int(
		zcbor_state_t *state, struct pointer_arg_uint0int *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_int32_decode(state, (&(*result).pointer_arg_uint0int)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_pointer_arg_uint1int(
		zcbor_state_t *state, struct pointer_arg_uint1int *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_int32_decode(state, (&(*result).pointer_arg_uint1int)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_pointer_btn(
		zcbor_state_t *state, struct pointer_btn_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).pointer_btn_choice, sizeof((*result).pointer_btn_choice)))) && ((((((*result).pointer_btn_choice == pointer_btn_pointer_left_m_c) && ((1)))
	|| (((*result).pointer_btn_choice == pointer_btn_pointer_right_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_pointer_arg_pointer_btn_m(
		zcbor_state_t *state, struct pointer_arg_pointer_btn_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (decode_pointer_btn(state, (&(*result).pointer_arg_pointer_btn_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_pointer_arg_uint3uint(
		zcbor_state_t *state, struct pointer_arg_uint3uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_uint32_decode(state, (&(*result).pointer_arg_uint3uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_pointer_arg_uint4int(
		zcbor_state_t *state, struct pointer_arg_uint4int *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (zcbor_int32_decode(state, (&(*result).pointer_arg_uint4int)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_pointer_arg(
		zcbor_state_t *state, struct pointer_arg *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && ((zcbor_present_decode(&((*result).pointer_arg_uint0int_present), (zcbor_decoder_t *)decode_repeated_pointer_arg_uint0int, state, (&(*result).pointer_arg_uint0int))
	&& zcbor_present_decode(&((*result).pointer_arg_uint1int_present), (zcbor_decoder_t *)decode_repeated_pointer_arg_uint1int, state, (&(*result).pointer_arg_uint1int))
	&& zcbor_present_decode(&((*result).pointer_arg_pointer_btn_m_present), (zcbor_decoder_t *)decode_repeated_pointer_arg_pointer_btn_m, state, (&(*result).pointer_arg_pointer_btn_m))
	&& zcbor_present_decode(&((*result).pointer_arg_uint3uint_present), (zcbor_decoder_t *)decode_repeated_pointer_arg_uint3uint, state, (&(*result).pointer_arg_uint3uint))
	&& zcbor_present_decode(&((*result).pointer_arg_uint4int_present), (zcbor_decoder_t *)decode_repeated_pointer_arg_uint4int, state, (&(*result).pointer_arg_uint4int))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_pointer_arg_uint0int(state, (&(*result).pointer_arg_uint0int));
		decode_repeated_pointer_arg_uint1int(state, (&(*result).pointer_arg_uint1int));
		decode_repeated_pointer_arg_pointer_btn_m(state, (&(*result).pointer_arg_pointer_btn_m));
		decode_repeated_pointer_arg_uint3uint(state, (&(*result).pointer_arg_uint3uint));
		decode_repeated_pointer_arg_uint4int(state, (&(*result).pointer_arg_uint4int));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_cmd_pointer_arg_m(
		zcbor_state_t *state, struct cmd_pointer_arg_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (9))))
	&& (decode_pointer_arg(state, (&(*result).cmd_pointer_arg_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_cmd(
		zcbor_state_t *state, struct cmd *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (decode_cmd_op(state, (&(*result).cmd_op_m))))
	&& zcbor_present_decode(&((*result).cmd_uint1uint_present), (zcbor_decoder_t *)decode_repeated_cmd_uint1uint, state, (&(*result).cmd_uint1uint))
	&& zcbor_present_decode(&((*result).cmd_uint2uint_present), (zcbor_decoder_t *)decode_repeated_cmd_uint2uint, state, (&(*result).cmd_uint2uint))
	&& zcbor_present_decode(&((*result).cmd_arg_m_present), (zcbor_decoder_t *)decode_repeated_cmd_arg_m, state, (&(*result).cmd_arg_m))
	&& zcbor_present_decode(&((*result).cmd_uint4uint_present), (zcbor_decoder_t *)decode_repeated_cmd_uint4uint, state, (&(*result).cmd_uint4uint))
	&& zcbor_present_decode(&((*result).cmd_uint5uint_present), (zcbor_decoder_t *)decode_repeated_cmd_uint5uint, state, (&(*result).cmd_uint5uint))
	&& zcbor_present_decode(&((*result).cmd_uint6bool_present), (zcbor_decoder_t *)decode_repeated_cmd_uint6bool, state, (&(*result).cmd_uint6bool))
	&& zcbor_present_decode(&((*result).cmd_answer_picks_m_present), (zcbor_decoder_t *)decode_repeated_cmd_answer_picks_m, state, (&(*result).cmd_answer_picks_m))
	&& zcbor_present_decode(&((*result).cmd_answer_aids_m_present), (zcbor_decoder_t *)decode_repeated_cmd_answer_aids_m, state, (&(*result).cmd_answer_aids_m))
	&& zcbor_present_decode(&((*result).cmd_pointer_arg_m_present), (zcbor_decoder_t *)decode_repeated_cmd_pointer_arg_m, state, (&(*result).cmd_pointer_arg_m))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_cmd_uint1uint(state, (&(*result).cmd_uint1uint));
		decode_repeated_cmd_uint2uint(state, (&(*result).cmd_uint2uint));
		decode_repeated_cmd_arg_m(state, (&(*result).cmd_arg_m));
		decode_repeated_cmd_uint4uint(state, (&(*result).cmd_uint4uint));
		decode_repeated_cmd_uint5uint(state, (&(*result).cmd_uint5uint));
		decode_repeated_cmd_uint6bool(state, (&(*result).cmd_uint6bool));
		decode_repeated_cmd_answer_picks_m(state, (&(*result).cmd_answer_picks_m));
		decode_repeated_cmd_answer_aids_m(state, (&(*result).cmd_answer_aids_m));
		decode_repeated_cmd_pointer_arg_m(state, (&(*result).cmd_pointer_arg_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_cmd(
		zcbor_state_t *state, struct msg_cmd *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (8))))
	&& ((decode_hdr(state, (&(*result).msg_cmd_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_cmd(state, (&(*result).msg_cmd_cmd_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_result_err(
		zcbor_state_t *state, struct result_err_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).result_err_choice, sizeof((*result).result_err_choice)))) && ((((((*result).result_err_choice == result_err_stale_m_c) && ((1)))
	|| (((*result).result_err_choice == result_err_denied_m_c) && ((1)))
	|| (((*result).result_err_choice == result_err_unsupported_m_c) && ((1)))
	|| (((*result).result_err_choice == result_err_no_window_m_c) && ((1)))
	|| (((*result).result_err_choice == result_err_timeout_m_c) && ((1)))
	|| (((*result).result_err_choice == result_err_internal_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_result_err_m(
		zcbor_state_t *state, struct result_err_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (decode_result_err(state, (&(*result).result_err_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_result_uint2tstr(
		zcbor_state_t *state, struct result_uint2tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_tstr_decode(state, (&(*result).result_uint2tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_scroll_pos_uint2uint(
		zcbor_state_t *state, struct scroll_pos_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).scroll_pos_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_scroll_pos_uint3tstr(
		zcbor_state_t *state, struct scroll_pos_uint3tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_tstr_decode(state, (&(*result).scroll_pos_uint3tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_scroll_pos(
		zcbor_state_t *state, struct scroll_pos *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).scroll_pos_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_bool_decode(state, (&(*result).scroll_pos_uint1bool))))
	&& zcbor_present_decode(&((*result).scroll_pos_uint2uint_present), (zcbor_decoder_t *)decode_repeated_scroll_pos_uint2uint, state, (&(*result).scroll_pos_uint2uint))
	&& zcbor_present_decode(&((*result).scroll_pos_uint3tstr_present), (zcbor_decoder_t *)decode_repeated_scroll_pos_uint3tstr, state, (&(*result).scroll_pos_uint3tstr))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_scroll_pos_uint2uint(state, (&(*result).scroll_pos_uint2uint));
		decode_repeated_scroll_pos_uint3tstr(state, (&(*result).scroll_pos_uint3tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_result_scroll_pos_m(
		zcbor_state_t *state, struct result_scroll_pos_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (decode_scroll_pos(state, (&(*result).result_scroll_pos_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_result(
		zcbor_state_t *state, struct result *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_bool_decode(state, (&(*result).result_uint0bool))))
	&& zcbor_present_decode(&((*result).result_err_m_present), (zcbor_decoder_t *)decode_repeated_result_err_m, state, (&(*result).result_err_m))
	&& zcbor_present_decode(&((*result).result_uint2tstr_present), (zcbor_decoder_t *)decode_repeated_result_uint2tstr, state, (&(*result).result_uint2tstr))
	&& zcbor_present_decode(&((*result).result_scroll_pos_m_present), (zcbor_decoder_t *)decode_repeated_result_scroll_pos_m, state, (&(*result).result_scroll_pos_m))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_result_err_m(state, (&(*result).result_err_m));
		decode_repeated_result_uint2tstr(state, (&(*result).result_uint2tstr));
		decode_repeated_result_scroll_pos_m(state, (&(*result).result_scroll_pos_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_result(
		zcbor_state_t *state, struct msg_result *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (9))))
	&& ((decode_hdr(state, (&(*result).msg_result_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_result(state, (&(*result).msg_result_result_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_detail_uint0uint(
		zcbor_state_t *state, struct detail_uint0uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).detail_uint0uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_detail_uint1uint(
		zcbor_state_t *state, struct detail_uint1uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).detail_uint1uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_detail_uint3uint(
		zcbor_state_t *state, struct detail_uint3uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_uint32_decode(state, (&(*result).detail_uint3uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_detail_uint4uint(
		zcbor_state_t *state, struct detail_uint4uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (zcbor_uint32_decode(state, (&(*result).detail_uint4uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_detail_uint5uint(
		zcbor_state_t *state, struct detail_uint5uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (5))))
	&& (zcbor_uint32_decode(state, (&(*result).detail_uint5uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_detail(
		zcbor_state_t *state, struct detail *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && ((zcbor_present_decode(&((*result).detail_uint0uint_present), (zcbor_decoder_t *)decode_repeated_detail_uint0uint, state, (&(*result).detail_uint0uint))
	&& zcbor_present_decode(&((*result).detail_uint1uint_present), (zcbor_decoder_t *)decode_repeated_detail_uint1uint, state, (&(*result).detail_uint1uint))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_tstr_decode(state, (&(*result).detail_uint2tstr))))
	&& zcbor_present_decode(&((*result).detail_uint3uint_present), (zcbor_decoder_t *)decode_repeated_detail_uint3uint, state, (&(*result).detail_uint3uint))
	&& zcbor_present_decode(&((*result).detail_uint4uint_present), (zcbor_decoder_t *)decode_repeated_detail_uint4uint, state, (&(*result).detail_uint4uint))
	&& zcbor_present_decode(&((*result).detail_uint5uint_present), (zcbor_decoder_t *)decode_repeated_detail_uint5uint, state, (&(*result).detail_uint5uint))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_detail_uint0uint(state, (&(*result).detail_uint0uint));
		decode_repeated_detail_uint1uint(state, (&(*result).detail_uint1uint));
		decode_repeated_detail_uint3uint(state, (&(*result).detail_uint3uint));
		decode_repeated_detail_uint4uint(state, (&(*result).detail_uint4uint));
		decode_repeated_detail_uint5uint(state, (&(*result).detail_uint5uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_detail(
		zcbor_state_t *state, struct msg_detail *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (10))))
	&& ((decode_hdr(state, (&(*result).msg_detail_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_detail(state, (&(*result).msg_detail_detail_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_cfg(
		zcbor_state_t *state, struct msg_cfg *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (11))))
	&& ((decode_hdr(state, (&(*result).msg_cfg_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_device_cfg(state, (&(*result).msg_cfg_device_cfg_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_batt(
		zcbor_state_t *state, struct batt *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).batt_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_bool_decode(state, (&(*result).batt_uint1bool))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_batt(
		zcbor_state_t *state, struct msg_batt *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (12))))
	&& ((decode_hdr(state, (&(*result).msg_batt_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_batt(state, (&(*result).msg_batt_batt_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_empty(
		zcbor_state_t *state, void *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_ping(
		zcbor_state_t *state, struct msg_ping *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (13))))
	&& ((decode_hdr(state, (&(*result).msg_ping_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_empty(state, NULL)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_pong(
		zcbor_state_t *state, struct msg_pong *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (14))))
	&& ((decode_hdr(state, (&(*result).msg_pong_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_empty(state, NULL)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_log_level(
		zcbor_state_t *state, struct log_level_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).log_level_choice, sizeof((*result).log_level_choice)))) && ((((((*result).log_level_choice == log_level_err_m_c) && ((1)))
	|| (((*result).log_level_choice == log_level_warn_m_c) && ((1)))
	|| (((*result).log_level_choice == log_level_info_m_c) && ((1)))
	|| (((*result).log_level_choice == log_level_debug_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_log(
		zcbor_state_t *state, struct log *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (decode_log_level(state, (&(*result).log_level_m))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_tstr_decode(state, (&(*result).log_uint1tstr))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_log(
		zcbor_state_t *state, struct msg_log *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (15))))
	&& ((decode_hdr(state, (&(*result).msg_log_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_log(state, (&(*result).msg_log_log_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_audio_codec(
		zcbor_state_t *state, struct audio_codec_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).audio_codec_choice, sizeof((*result).audio_codec_choice)))) && ((((((*result).audio_codec_choice == audio_codec_adpcm16k_m_c) && ((1)))
	|| (((*result).audio_codec_choice == audio_codec_opus16k_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_audio_start_uint2uint(
		zcbor_state_t *state, struct audio_start_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).audio_start_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_audio_start(
		zcbor_state_t *state, struct audio_start *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).audio_start_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (decode_audio_codec(state, (&(*result).audio_start_audio_codec_m))))
	&& zcbor_present_decode(&((*result).audio_start_uint2uint_present), (zcbor_decoder_t *)decode_repeated_audio_start_uint2uint, state, (&(*result).audio_start_uint2uint))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_audio_start_uint2uint(state, (&(*result).audio_start_uint2uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_audio_start(
		zcbor_state_t *state, struct msg_audio_start *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (16))))
	&& ((decode_hdr(state, (&(*result).msg_audio_start_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_audio_start(state, (&(*result).msg_audio_start_audio_start_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_audio(
		zcbor_state_t *state, struct audio *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).audio_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).audio_uint1uint))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_bstr_decode(state, (&(*result).audio_uint2bstr)))
	&& ((((*result).audio_uint2bstr.len >= 0)
	&& ((*result).audio_uint2bstr.len <= 240)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_audio(
		zcbor_state_t *state, struct msg_audio *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (17))))
	&& ((decode_hdr(state, (&(*result).msg_audio_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_audio(state, (&(*result).msg_audio_audio_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_audio_end(
		zcbor_state_t *state, struct audio_end *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).audio_end_uint0uint))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_audio_end(
		zcbor_state_t *state, struct msg_audio_end *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (18))))
	&& ((decode_hdr(state, (&(*result).msg_audio_end_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_audio_end(state, (&(*result).msg_audio_end_audio_end_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_transcript(
		zcbor_state_t *state, struct transcript *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).transcript_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_tstr_decode(state, (&(*result).transcript_uint1tstr))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).transcript_uint2uint))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_transcript(
		zcbor_state_t *state, struct msg_transcript *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (19))))
	&& ((decode_hdr(state, (&(*result).msg_transcript_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_transcript(state, (&(*result).msg_transcript_transcript_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_kb_start(
		zcbor_state_t *state, struct kb_start *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).kb_start_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).kb_start_uint1uint))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_bstr_decode(state, (&(*result).kb_start_uint2bstr)))
	&& ((((*result).kb_start_uint2bstr.len >= 0)
	&& ((*result).kb_start_uint2bstr.len <= 640)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_kb_start(
		zcbor_state_t *state, struct msg_kb_start *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (20))))
	&& ((decode_hdr(state, (&(*result).msg_kb_start_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_kb_start(state, (&(*result).msg_kb_start_kb_start_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_kb_stroke(
		zcbor_state_t *state, struct kb_stroke *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).kb_stroke_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).kb_stroke_uint1uint))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_bstr_decode(state, (&(*result).kb_stroke_uint2bstr)))
	&& ((((*result).kb_stroke_uint2bstr.len >= 0)
	&& ((*result).kb_stroke_uint2bstr.len <= 1200)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_kb_stroke(
		zcbor_state_t *state, struct msg_kb_stroke *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (21))))
	&& ((decode_hdr(state, (&(*result).msg_kb_stroke_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_kb_stroke(state, (&(*result).msg_kb_stroke_kb_stroke_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_kb_cands_uint3uint(
		zcbor_state_t *state, struct kb_cands_uint3uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_uint32_decode(state, (&(*result).kb_cands_uint3uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_kb_cands_result_err_m(
		zcbor_state_t *state, struct kb_cands_result_err_m *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (decode_result_err(state, (&(*result).kb_cands_result_err_m)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_kb_cands(
		zcbor_state_t *state, struct kb_cands *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).kb_cands_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).kb_cands_uint1uint))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 4, &(*result).kb_cands_tstr_l_tstr_count, (zcbor_decoder_t *)zcbor_tstr_decode, state, (*&(*result).kb_cands_tstr_l_tstr), sizeof(struct zcbor_string))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& zcbor_present_decode(&((*result).kb_cands_uint3uint_present), (zcbor_decoder_t *)decode_repeated_kb_cands_uint3uint, state, (&(*result).kb_cands_uint3uint))
	&& zcbor_present_decode(&((*result).kb_cands_result_err_m_present), (zcbor_decoder_t *)decode_repeated_kb_cands_result_err_m, state, (&(*result).kb_cands_result_err_m))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_tstr_decode(state, (*&(*result).kb_cands_tstr_l_tstr));
		decode_repeated_kb_cands_uint3uint(state, (&(*result).kb_cands_uint3uint));
		decode_repeated_kb_cands_result_err_m(state, (&(*result).kb_cands_result_err_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_kb_cands(
		zcbor_state_t *state, struct msg_kb_cands *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (22))))
	&& ((decode_hdr(state, (&(*result).msg_kb_cands_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_kb_cands(state, (&(*result).msg_kb_cands_kb_cands_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_usage_uint1uint(
		zcbor_state_t *state, struct usage_uint1uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).usage_uint1uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_usage_uint2uint(
		zcbor_state_t *state, struct usage_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).usage_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_usage_uint3uint(
		zcbor_state_t *state, struct usage_uint3uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (3))))
	&& (zcbor_uint32_decode(state, (&(*result).usage_uint3uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_usage_uint4uint(
		zcbor_state_t *state, struct usage_uint4uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (4))))
	&& (zcbor_uint32_decode(state, (&(*result).usage_uint4uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_usage_uint5uint(
		zcbor_state_t *state, struct usage_uint5uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (5))))
	&& (zcbor_uint32_decode(state, (&(*result).usage_uint5uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_usage_uint6uint(
		zcbor_state_t *state, struct usage_uint6uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (6))))
	&& (zcbor_uint32_decode(state, (&(*result).usage_uint6uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_usage(
		zcbor_state_t *state, struct usage *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).usage_uint0uint))))
	&& zcbor_present_decode(&((*result).usage_uint1uint_present), (zcbor_decoder_t *)decode_repeated_usage_uint1uint, state, (&(*result).usage_uint1uint))
	&& zcbor_present_decode(&((*result).usage_uint2uint_present), (zcbor_decoder_t *)decode_repeated_usage_uint2uint, state, (&(*result).usage_uint2uint))
	&& zcbor_present_decode(&((*result).usage_uint3uint_present), (zcbor_decoder_t *)decode_repeated_usage_uint3uint, state, (&(*result).usage_uint3uint))
	&& zcbor_present_decode(&((*result).usage_uint4uint_present), (zcbor_decoder_t *)decode_repeated_usage_uint4uint, state, (&(*result).usage_uint4uint))
	&& zcbor_present_decode(&((*result).usage_uint5uint_present), (zcbor_decoder_t *)decode_repeated_usage_uint5uint, state, (&(*result).usage_uint5uint))
	&& zcbor_present_decode(&((*result).usage_uint6uint_present), (zcbor_decoder_t *)decode_repeated_usage_uint6uint, state, (&(*result).usage_uint6uint))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_usage_uint1uint(state, (&(*result).usage_uint1uint));
		decode_repeated_usage_uint2uint(state, (&(*result).usage_uint2uint));
		decode_repeated_usage_uint3uint(state, (&(*result).usage_uint3uint));
		decode_repeated_usage_uint4uint(state, (&(*result).usage_uint4uint));
		decode_repeated_usage_uint5uint(state, (&(*result).usage_uint5uint));
		decode_repeated_usage_uint6uint(state, (&(*result).usage_uint6uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_usage(
		zcbor_state_t *state, struct msg_usage *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (23))))
	&& ((decode_hdr(state, (&(*result).msg_usage_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_usage(state, (&(*result).msg_usage_usage_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_fw_begin_uint2tstr(
		zcbor_state_t *state, struct fw_begin_uint2tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_tstr_decode(state, (&(*result).fw_begin_uint2tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_fw_begin(
		zcbor_state_t *state, struct fw_begin *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).fw_begin_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_bstr_decode(state, (&(*result).fw_begin_uint1bstr)))
	&& ((((*result).fw_begin_uint1bstr.len == 32)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))
	&& zcbor_present_decode(&((*result).fw_begin_uint2tstr_present), (zcbor_decoder_t *)decode_repeated_fw_begin_uint2tstr, state, (&(*result).fw_begin_uint2tstr))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_fw_begin_uint2tstr(state, (&(*result).fw_begin_uint2tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_fw_begin(
		zcbor_state_t *state, struct msg_fw_begin *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (24))))
	&& ((decode_hdr(state, (&(*result).msg_fw_begin_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_fw_begin(state, (&(*result).msg_fw_begin_fw_begin_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_fw_chunk(
		zcbor_state_t *state, struct fw_chunk *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).fw_chunk_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_bstr_decode(state, (&(*result).fw_chunk_uint1bstr)))
	&& ((((*result).fw_chunk_uint1bstr.len >= 0)
	&& ((*result).fw_chunk_uint1bstr.len <= 2048)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_fw_chunk(
		zcbor_state_t *state, struct msg_fw_chunk *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (25))))
	&& ((decode_hdr(state, (&(*result).msg_fw_chunk_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_fw_chunk(state, (&(*result).msg_fw_chunk_fw_chunk_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_fw_status(
		zcbor_state_t *state, struct fw_status_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).fw_status_choice, sizeof((*result).fw_status_choice)))) && ((((((*result).fw_status_choice == fw_status_go_m_c) && ((1)))
	|| (((*result).fw_status_choice == fw_status_done_m_c) && ((1)))
	|| (((*result).fw_status_choice == fw_status_busy_m_c) && ((1)))
	|| (((*result).fw_status_choice == fw_status_bad_m_c) && ((1)))
	|| (((*result).fw_status_choice == fw_status_fail_m_c) && ((1)))
	|| (((*result).fw_status_choice == fw_status_stopped_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_fw_ack_uint2tstr(
		zcbor_state_t *state, struct fw_ack_uint2tstr *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_tstr_decode(state, (&(*result).fw_ack_uint2tstr)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_fw_ack(
		zcbor_state_t *state, struct fw_ack *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (decode_fw_status(state, (&(*result).fw_ack_fw_status_m))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).fw_ack_uint1uint))))
	&& zcbor_present_decode(&((*result).fw_ack_uint2tstr_present), (zcbor_decoder_t *)decode_repeated_fw_ack_uint2tstr, state, (&(*result).fw_ack_uint2tstr))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_fw_ack_uint2tstr(state, (&(*result).fw_ack_uint2tstr));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_fw_ack(
		zcbor_state_t *state, struct msg_fw_ack *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (26))))
	&& ((decode_hdr(state, (&(*result).msg_fw_ack_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_fw_ack(state, (&(*result).msg_fw_ack_fw_ack_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_power(
		zcbor_state_t *state, struct power *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint1uint))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint2uint))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (zcbor_bool_decode(state, (&(*result).power_uint3bool))))
	&& (((zcbor_uint32_expect(state, (4))))
	&& (zcbor_bool_decode(state, (&(*result).power_uint4bool))))
	&& (((zcbor_uint32_expect(state, (5))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint5uint))))
	&& (((zcbor_uint32_expect(state, (6))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint6uint))))
	&& (((zcbor_uint32_expect(state, (7))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint7uint))))
	&& (((zcbor_uint32_expect(state, (8))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint8uint))))
	&& (((zcbor_uint32_expect(state, (9))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint9uint))))
	&& (((zcbor_uint32_expect(state, (10))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint10uint))))
	&& (((zcbor_uint32_expect(state, (11))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint11uint))))
	&& (((zcbor_uint32_expect(state, (12))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint12uint))))
	&& (((zcbor_uint32_expect(state, (13))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint13uint))))
	&& (((zcbor_uint32_expect(state, (14))))
	&& (zcbor_uint32_decode(state, (&(*result).power_uint14uint))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_power(
		zcbor_state_t *state, struct msg_power *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (27))))
	&& ((decode_hdr(state, (&(*result).msg_power_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_power(state, (&(*result).msg_power_power_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_dbg_ui_state_req(
		zcbor_state_t *state, struct msg_dbg_ui_state_req *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (64))))
	&& ((decode_hdr(state, (&(*result).msg_dbg_ui_state_req_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_empty(state, NULL)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_dbg_ui_state_uint1uint(
		zcbor_state_t *state, struct dbg_ui_state_uint1uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).dbg_ui_state_uint1uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_dbg_ui_state_uint2uint(
		zcbor_state_t *state, struct dbg_ui_state_uint2uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).dbg_ui_state_uint2uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_dbg_ui_state_uint5uint(
		zcbor_state_t *state, struct dbg_ui_state_uint5uint *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (5))))
	&& (zcbor_uint32_decode(state, (&(*result).dbg_ui_state_uint5uint)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_dbg_ui_state(
		zcbor_state_t *state, struct dbg_ui_state *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_tstr_decode(state, (&(*result).dbg_ui_state_uint0tstr))))
	&& zcbor_present_decode(&((*result).dbg_ui_state_uint1uint_present), (zcbor_decoder_t *)decode_repeated_dbg_ui_state_uint1uint, state, (&(*result).dbg_ui_state_uint1uint))
	&& zcbor_present_decode(&((*result).dbg_ui_state_uint2uint_present), (zcbor_decoder_t *)decode_repeated_dbg_ui_state_uint2uint, state, (&(*result).dbg_ui_state_uint2uint))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 8, &(*result).dbg_ui_state_tstr_l_tstr_count, (zcbor_decoder_t *)zcbor_tstr_decode, state, (*&(*result).dbg_ui_state_tstr_l_tstr), sizeof(struct zcbor_string))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& (((zcbor_uint32_expect(state, (4))))
	&& (zcbor_tstr_decode(state, (&(*result).dbg_ui_state_uint4tstr))))
	&& zcbor_present_decode(&((*result).dbg_ui_state_uint5uint_present), (zcbor_decoder_t *)decode_repeated_dbg_ui_state_uint5uint, state, (&(*result).dbg_ui_state_uint5uint))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_tstr_decode(state, (*&(*result).dbg_ui_state_tstr_l_tstr));
		decode_repeated_dbg_ui_state_uint1uint(state, (&(*result).dbg_ui_state_uint1uint));
		decode_repeated_dbg_ui_state_uint2uint(state, (&(*result).dbg_ui_state_uint2uint));
		decode_repeated_dbg_ui_state_uint5uint(state, (&(*result).dbg_ui_state_uint5uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_dbg_ui_state(
		zcbor_state_t *state, struct msg_dbg_ui_state *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (65))))
	&& ((decode_hdr(state, (&(*result).msg_dbg_ui_state_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_dbg_ui_state(state, (&(*result).msg_dbg_ui_state_dbg_ui_state_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_dbg_screenshot_req(
		zcbor_state_t *state, struct msg_dbg_screenshot_req *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (66))))
	&& ((decode_hdr(state, (&(*result).msg_dbg_screenshot_req_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_empty(state, NULL)))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_dbg_screenshot_chunk(
		zcbor_state_t *state, struct dbg_screenshot_chunk *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_decode(state, (&(*result).dbg_screenshot_chunk_uint0uint))))
	&& (((zcbor_uint32_expect(state, (1))))
	&& (zcbor_uint32_decode(state, (&(*result).dbg_screenshot_chunk_uint1uint))))
	&& (((zcbor_uint32_expect(state, (2))))
	&& (zcbor_uint32_decode(state, (&(*result).dbg_screenshot_chunk_uint2uint))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (zcbor_bstr_decode(state, (&(*result).dbg_screenshot_chunk_uint3bstr))))
	&& (((zcbor_uint32_expect(state, (4))))
	&& (zcbor_bool_decode(state, (&(*result).dbg_screenshot_chunk_uint4bool))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_dbg_screenshot_chunk(
		zcbor_state_t *state, struct msg_dbg_screenshot_chunk *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (67))))
	&& ((decode_hdr(state, (&(*result).msg_dbg_screenshot_chunk_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_dbg_screenshot_chunk(state, (&(*result).msg_dbg_screenshot_chunk_dbg_screenshot_chunk_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_input_event(
		zcbor_state_t *state, struct input_event_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((((zcbor_uint_decode(state, &(*result).input_event_choice, sizeof((*result).input_event_choice)))) && ((((((*result).input_event_choice == input_event_tap_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_double_tap_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_long_press_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_swipe_up_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_swipe_down_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_swipe_left_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_swipe_right_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_btn_a_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_btn_b_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_btn_a_long_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_btn_b_long_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_lift_m_c) && ((1)))
	|| (((*result).input_event_choice == input_event_shake_m_c) && ((1)))) || (zcbor_error(state, ZCBOR_ERR_WRONG_VALUE), false))))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_dbg_inject_input_uint1int(
		zcbor_state_t *state, struct dbg_inject_input_uint1int *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (1))))
	&& (zcbor_int32_decode(state, (&(*result).dbg_inject_input_uint1int)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_dbg_inject_input_uint2int(
		zcbor_state_t *state, struct dbg_inject_input_uint2int *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_expect(state, (2))))
	&& (zcbor_int32_decode(state, (&(*result).dbg_inject_input_uint2int)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_dbg_inject_input(
		zcbor_state_t *state, struct dbg_inject_input *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (decode_input_event(state, (&(*result).dbg_inject_input_input_event_m))))
	&& zcbor_present_decode(&((*result).dbg_inject_input_uint1int_present), (zcbor_decoder_t *)decode_repeated_dbg_inject_input_uint1int, state, (&(*result).dbg_inject_input_uint1int))
	&& zcbor_present_decode(&((*result).dbg_inject_input_uint2int_present), (zcbor_decoder_t *)decode_repeated_dbg_inject_input_uint2int, state, (&(*result).dbg_inject_input_uint2int))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_dbg_inject_input_uint1int(state, (&(*result).dbg_inject_input_uint1int));
		decode_repeated_dbg_inject_input_uint2int(state, (&(*result).dbg_inject_input_uint2int));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_dbg_inject_input(
		zcbor_state_t *state, struct msg_dbg_inject_input *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (68))))
	&& ((decode_hdr(state, (&(*result).msg_dbg_inject_input_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_dbg_inject_input(state, (&(*result).msg_dbg_inject_input_dbg_inject_input_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_dbg_set_log_level(
		zcbor_state_t *state, struct dbg_set_log_level *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (decode_log_level(state, (&(*result).dbg_set_log_level_log_level_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_msg_dbg_set_log_level(
		zcbor_state_t *state, struct msg_dbg_set_log_level *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_uint32_expect(state, (0))))
	&& (zcbor_uint32_expect(state, (69))))
	&& ((decode_hdr(state, (&(*result).msg_dbg_set_log_level_hdr_m))))
	&& (((zcbor_uint32_expect(state, (3))))
	&& (decode_dbg_set_log_level(state, (&(*result).msg_dbg_set_log_level_dbg_set_log_level_m))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_envelope(
		zcbor_state_t *state, struct envelope_r *result)
{
	zcbor_log("%s\r\n", __func__);
	bool int_res;

	bool res = (((zcbor_union_start_code(state) && (int_res = ((((decode_msg_hello(state, (&(*result).envelope_msg_hello_m)))) && (((*result).envelope_choice = envelope_msg_hello_m_c), true))
	|| (zcbor_union_elem_code(state) && (((decode_msg_welcome(state, (&(*result).envelope_msg_welcome_m)))) && (((*result).envelope_choice = envelope_msg_welcome_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_snapshot(state, (&(*result).envelope_msg_snapshot_m)))) && (((*result).envelope_choice = envelope_msg_snapshot_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_session_upd(state, (&(*result).envelope_msg_session_upd_m)))) && (((*result).envelope_choice = envelope_msg_session_upd_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_gone(state, (&(*result).envelope_msg_gone_m)))) && (((*result).envelope_choice = envelope_msg_gone_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_attention(state, (&(*result).envelope_msg_attention_m)))) && (((*result).envelope_choice = envelope_msg_attention_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_clear(state, (&(*result).envelope_msg_clear_m)))) && (((*result).envelope_choice = envelope_msg_clear_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_cmd(state, (&(*result).envelope_msg_cmd_m)))) && (((*result).envelope_choice = envelope_msg_cmd_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_result(state, (&(*result).envelope_msg_result_m)))) && (((*result).envelope_choice = envelope_msg_result_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_detail(state, (&(*result).envelope_msg_detail_m)))) && (((*result).envelope_choice = envelope_msg_detail_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_cfg(state, (&(*result).envelope_msg_cfg_m)))) && (((*result).envelope_choice = envelope_msg_cfg_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_batt(state, (&(*result).envelope_msg_batt_m)))) && (((*result).envelope_choice = envelope_msg_batt_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_ping(state, (&(*result).envelope_msg_ping_m)))) && (((*result).envelope_choice = envelope_msg_ping_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_pong(state, (&(*result).envelope_msg_pong_m)))) && (((*result).envelope_choice = envelope_msg_pong_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_log(state, (&(*result).envelope_msg_log_m)))) && (((*result).envelope_choice = envelope_msg_log_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_audio_start(state, (&(*result).envelope_msg_audio_start_m)))) && (((*result).envelope_choice = envelope_msg_audio_start_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_audio(state, (&(*result).envelope_msg_audio_m)))) && (((*result).envelope_choice = envelope_msg_audio_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_audio_end(state, (&(*result).envelope_msg_audio_end_m)))) && (((*result).envelope_choice = envelope_msg_audio_end_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_transcript(state, (&(*result).envelope_msg_transcript_m)))) && (((*result).envelope_choice = envelope_msg_transcript_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_kb_start(state, (&(*result).envelope_msg_kb_start_m)))) && (((*result).envelope_choice = envelope_msg_kb_start_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_kb_stroke(state, (&(*result).envelope_msg_kb_stroke_m)))) && (((*result).envelope_choice = envelope_msg_kb_stroke_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_kb_cands(state, (&(*result).envelope_msg_kb_cands_m)))) && (((*result).envelope_choice = envelope_msg_kb_cands_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_usage(state, (&(*result).envelope_msg_usage_m)))) && (((*result).envelope_choice = envelope_msg_usage_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_fw_begin(state, (&(*result).envelope_msg_fw_begin_m)))) && (((*result).envelope_choice = envelope_msg_fw_begin_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_fw_chunk(state, (&(*result).envelope_msg_fw_chunk_m)))) && (((*result).envelope_choice = envelope_msg_fw_chunk_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_fw_ack(state, (&(*result).envelope_msg_fw_ack_m)))) && (((*result).envelope_choice = envelope_msg_fw_ack_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_power(state, (&(*result).envelope_msg_power_m)))) && (((*result).envelope_choice = envelope_msg_power_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_dbg_ui_state_req(state, (&(*result).envelope_msg_dbg_ui_state_req_m)))) && (((*result).envelope_choice = envelope_msg_dbg_ui_state_req_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_dbg_ui_state(state, (&(*result).envelope_msg_dbg_ui_state_m)))) && (((*result).envelope_choice = envelope_msg_dbg_ui_state_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_dbg_screenshot_req(state, (&(*result).envelope_msg_dbg_screenshot_req_m)))) && (((*result).envelope_choice = envelope_msg_dbg_screenshot_req_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_dbg_screenshot_chunk(state, (&(*result).envelope_msg_dbg_screenshot_chunk_m)))) && (((*result).envelope_choice = envelope_msg_dbg_screenshot_chunk_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_dbg_inject_input(state, (&(*result).envelope_msg_dbg_inject_input_m)))) && (((*result).envelope_choice = envelope_msg_dbg_inject_input_m_c), true)))
	|| (zcbor_union_elem_code(state) && (((decode_msg_dbg_set_log_level(state, (&(*result).envelope_msg_dbg_set_log_level_m)))) && (((*result).envelope_choice = envelope_msg_dbg_set_log_level_m_c), true)))), zcbor_union_end_code(state), int_res))));

	log_result(state, res, __func__);
	return res;
}



int cbor_decode_envelope(
		const uint8_t *payload, size_t payload_len,
		struct envelope_r *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[9];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_envelope, sizeof(states) / sizeof(zcbor_state_t), 1);
}
