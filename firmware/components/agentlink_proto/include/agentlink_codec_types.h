/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 3
 */

#ifndef AGENTLINK_CODEC_TYPES_H__
#define AGENTLINK_CODEC_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zcbor_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which value for --default-max-qty this file was created with.
 *
 *  The define is used in the other generated file to do a build-time
 *  compatibility check.
 *
 *  See `zcbor --help` for more information about --default-max-qty
 */
#define DEFAULT_MAX_QTY 3

struct hdr_uint2uint {
	uint32_t hdr_uint2uint;
};

struct hdr_r {
	uint32_t hdr_uint1uint;
	struct hdr_uint2uint hdr_uint2uint;
	bool hdr_uint2uint_present;
};

struct device_class_r {
	enum {
		device_class_watch_m_c = 0,
		device_class_desk_m_c = 1,
		device_class_pad_m_c = 2,
		device_class_sim_m_c = 3,
		device_class_other_m_c = 15,
	} device_class_choice;
};

struct device_info {
	struct device_class_r device_info_device_class_m;
	struct zcbor_string device_info_uint1tstr;
	struct zcbor_string device_info_uint2tstr;
	struct zcbor_string device_info_uint3bstr;
};

struct disp_shape_r {
	enum {
		disp_shape_round_m_c = 0,
		disp_shape_rect_m_c = 1,
	} disp_shape_choice;
};

struct display_caps {
	struct disp_shape_r display_caps_disp_shape_m;
	uint32_t display_caps_uint1uint;
	uint32_t display_caps_uint2uint;
	bool display_caps_uint3bool;
};

struct display_caps_m {
	struct display_caps display_caps_m;
};

struct input_kind_r {
	enum {
		input_kind_touch_m_c = 0,
		input_kind_btn_m_c = 1,
		input_kind_rotary_m_c = 2,
		input_kind_imu_m_c = 3,
	} input_kind_choice;
};

struct caps_uint2uint {
	uint32_t caps_uint2uint;
};

struct feature_r {
	enum {
		feature_focus_m_c = 0,
		feature_ack_m_c = 1,
		feature_approve_m_c = 2,
		feature_reply_m_c = 3,
		feature_voice_m_c = 4,
		feature_badge_m_c = 5,
		feature_debug_m_c = 6,
		feature_scroll_m_c = 7,
		feature_keys_m_c = 8,
	} feature_choice;
};

struct caps {
	struct display_caps_m display_caps_m;
	bool display_caps_m_present;
	struct input_kind_r caps_input_kind_m_l_input_kind_m[4];
	size_t caps_input_kind_m_l_input_kind_m_count;
	struct caps_uint2uint caps_uint2uint;
	bool caps_uint2uint_present;
	bool caps_uint3bool;
	bool caps_uint4bool;
	bool caps_uint5bool;
	bool caps_uint6bool;
	uint32_t caps_uint7uint;
	struct feature_r caps_feature_m_l_feature_m[12];
	size_t caps_feature_m_l_feature_m_count;
};

struct hello_uint3uint {
	uint32_t hello_uint3uint;
};

struct hello {
	uint32_t hello_major_l_major;
	uint32_t hello_major_l_minor;
	struct device_info hello_device_info_m;
	struct caps hello_caps_m;
	struct hello_uint3uint hello_uint3uint;
	bool hello_uint3uint_present;
};

struct msg_hello {
	struct hdr_r msg_hello_hdr_m;
	struct hello msg_hello_hello_m;
};

struct host_os_r {
	enum {
		host_os_mac_m_c = 0,
		host_os_win_m_c = 1,
		host_os_linux_m_c = 2,
	} host_os_choice;
};

struct host_info {
	struct zcbor_string host_info_uint0bstr;
	struct zcbor_string host_info_uint1tstr;
	struct host_os_r host_info_host_os_m;
};

struct time_sync {
	uint32_t time_sync_uint0uint;
	int32_t time_sync_uint1int;
};

struct privacy_r {
	enum {
		privacy_meta_m_c = 0,
		privacy_preview_m_c = 1,
		privacy_full_m_c = 2,
	} privacy_choice;
};

struct device_cfg_privacy_m {
	struct privacy_r device_cfg_privacy_m;
};

struct alert_mode_r {
	enum {
		alert_mode_full_m_c = 0,
		alert_mode_quiet_m_c = 1,
		alert_mode_silent_m_c = 2,
	} alert_mode_choice;
};

struct device_cfg_alert_mode_m {
	struct alert_mode_r device_cfg_alert_mode_m;
};

struct device_cfg_uint2uint {
	uint32_t device_cfg_uint2uint;
};

struct device_cfg_uint3uint {
	uint32_t device_cfg_uint3uint;
};

struct device_cfg_uint4tstr {
	struct zcbor_string device_cfg_uint4tstr;
};

struct device_cfg {
	struct device_cfg_privacy_m device_cfg_privacy_m;
	bool device_cfg_privacy_m_present;
	struct device_cfg_alert_mode_m device_cfg_alert_mode_m;
	bool device_cfg_alert_mode_m_present;
	struct device_cfg_uint2uint device_cfg_uint2uint;
	bool device_cfg_uint2uint_present;
	struct device_cfg_uint3uint device_cfg_uint3uint;
	bool device_cfg_uint3uint_present;
	struct device_cfg_uint4tstr device_cfg_uint4tstr;
	bool device_cfg_uint4tstr_present;
};

struct welcome_tstr_l_r {
	struct zcbor_string welcome_tstr_l_tstr[6];
	size_t welcome_tstr_l_tstr_count;
};

struct welcome_uint5bool {
	bool welcome_uint5bool;
};

struct welcome_uint6bool {
	bool welcome_uint6bool;
};

struct welcome {
	uint32_t welcome_major_l_major;
	uint32_t welcome_major_l_minor;
	struct host_info welcome_host_info_m;
	struct time_sync welcome_time_sync_m;
	struct device_cfg welcome_device_cfg_m;
	struct welcome_tstr_l_r welcome_tstr_l;
	bool welcome_tstr_l_present;
	struct welcome_uint5bool welcome_uint5bool;
	bool welcome_uint5bool_present;
	struct welcome_uint6bool welcome_uint6bool;
	bool welcome_uint6bool_present;
};

struct msg_welcome {
	struct hdr_r msg_welcome_hdr_m;
	struct welcome msg_welcome_welcome_m;
};

struct agent_r {
	enum {
		agent_copilot_m_c = 0,
		agent_claude_m_c = 1,
		agent_cloud_m_c = 2,
		agent_shell_m_c = 3,
		agent_other_m_c = 15,
	} agent_choice;
};

struct session_agent_m {
	struct agent_r session_agent_m;
};

struct sess_state_r {
	enum {
		sess_state_start_m_c = 0,
		sess_state_work_m_c = 1,
		sess_state_input_m_c = 2,
		sess_state_error_m_c = 3,
		sess_state_done_m_c = 4,
		sess_state_idle_m_c = 5,
		sess_state_ended_m_c = 6,
	} sess_state_choice;
};

struct session_sess_state_m {
	struct sess_state_r session_sess_state_m;
};

struct input_sub_r {
	enum {
		input_sub_approval_m_c = 0,
		input_sub_question_m_c = 1,
	} input_sub_choice;
};

struct session_input_sub_m {
	struct input_sub_r session_input_sub_m;
};

struct session_uint4tstr {
	struct zcbor_string session_uint4tstr;
};

struct session_uint5tstr {
	struct zcbor_string session_uint5tstr;
};

struct session_uint6tstr {
	struct zcbor_string session_uint6tstr;
};

struct session_uint7uint {
	uint32_t session_uint7uint;
};

struct session_uint8uint {
	uint32_t session_uint8uint;
};

struct session_uint9tstr {
	struct zcbor_string session_uint9tstr;
};

struct session_uint10bool {
	bool session_uint10bool;
};

struct session_uint11uint {
	uint32_t session_uint11uint;
};

struct session_uint12tstr {
	struct zcbor_string session_uint12tstr;
};

struct session_uint13uint {
	uint32_t session_uint13uint;
};

struct session_uint14bool {
	bool session_uint14bool;
};

struct session_uint15uint {
	uint32_t session_uint15uint;
};

struct session_uint16bool {
	bool session_uint16bool;
};

struct session_uint17tstr {
	struct zcbor_string session_uint17tstr;
};

struct session_tstr_l_r {
	struct zcbor_string session_tstr_l_tstr[3];
	size_t session_tstr_l_tstr_count;
};

struct session_uint19uint {
	uint32_t session_uint19uint;
};

struct session_uint20uint {
	uint32_t session_uint20uint;
};

struct session_uint21uint {
	uint32_t session_uint21uint;
};

struct session_uint22tstr {
	struct zcbor_string session_uint22tstr;
};

struct perm_mode_r {
	enum {
		perm_mode_default_m_c = 0,
		perm_mode_plan_m_c = 1,
		perm_mode_accept_edits_m_c = 2,
		perm_mode_auto_m_c = 3,
		perm_mode_dont_ask_m_c = 4,
		perm_mode_bypass_m_c = 5,
	} perm_mode_choice;
};

struct session_perm_mode_m {
	struct perm_mode_r session_perm_mode_m;
};

struct session_uint24uint {
	uint32_t session_uint24uint;
};

struct pr_state_r {
	enum {
		pr_state_none_m_c = 0,
		pr_state_pending_m_c = 1,
		pr_state_approved_m_c = 2,
		pr_state_changes_m_c = 3,
		pr_state_draft_m_c = 4,
	} pr_state_choice;
};

struct session_pr_state_m {
	struct pr_state_r session_pr_state_m;
};

struct session {
	uint32_t session_uint0uint;
	struct session_agent_m session_agent_m;
	bool session_agent_m_present;
	struct session_sess_state_m session_sess_state_m;
	bool session_sess_state_m_present;
	struct session_input_sub_m session_input_sub_m;
	bool session_input_sub_m_present;
	struct session_uint4tstr session_uint4tstr;
	bool session_uint4tstr_present;
	struct session_uint5tstr session_uint5tstr;
	bool session_uint5tstr_present;
	struct session_uint6tstr session_uint6tstr;
	bool session_uint6tstr_present;
	struct session_uint7uint session_uint7uint;
	bool session_uint7uint_present;
	struct session_uint8uint session_uint8uint;
	bool session_uint8uint_present;
	struct session_uint9tstr session_uint9tstr;
	bool session_uint9tstr_present;
	struct session_uint10bool session_uint10bool;
	bool session_uint10bool_present;
	struct session_uint11uint session_uint11uint;
	bool session_uint11uint_present;
	struct session_uint12tstr session_uint12tstr;
	bool session_uint12tstr_present;
	struct session_uint13uint session_uint13uint;
	bool session_uint13uint_present;
	struct session_uint14bool session_uint14bool;
	bool session_uint14bool_present;
	struct session_uint15uint session_uint15uint;
	bool session_uint15uint_present;
	struct session_uint16bool session_uint16bool;
	bool session_uint16bool_present;
	struct session_uint17tstr session_uint17tstr;
	bool session_uint17tstr_present;
	struct session_tstr_l_r session_tstr_l;
	bool session_tstr_l_present;
	struct session_uint19uint session_uint19uint;
	bool session_uint19uint_present;
	struct session_uint20uint session_uint20uint;
	bool session_uint20uint_present;
	struct session_uint21uint session_uint21uint;
	bool session_uint21uint_present;
	struct session_uint22tstr session_uint22tstr;
	bool session_uint22tstr_present;
	struct session_perm_mode_m session_perm_mode_m;
	bool session_perm_mode_m_present;
	struct session_uint24uint session_uint24uint;
	bool session_uint24uint_present;
	struct session_pr_state_m session_pr_state_m;
	bool session_pr_state_m_present;
};

struct att_kind_r {
	enum {
		att_kind_approval_m_c = 0,
		att_kind_question_m_c = 1,
		att_kind_error_m_c = 2,
		att_kind_done_m_c = 3,
	} att_kind_choice;
};

struct away_event {
	uint32_t away_event_uint0uint;
	struct att_kind_r away_event_att_kind_m;
	uint32_t away_event_uint2uint;
};

struct snapshot_away_event_m_l_r {
	struct away_event snapshot_away_event_m_l_away_event_m[16];
	size_t snapshot_away_event_m_l_away_event_m_count;
};

struct snapshot {
	struct session snapshot_session_m_l_session_m[32];
	size_t snapshot_session_m_l_session_m_count;
	struct snapshot_away_event_m_l_r snapshot_away_event_m_l;
	bool snapshot_away_event_m_l_present;
};

struct msg_snapshot {
	struct hdr_r msg_snapshot_hdr_m;
	struct snapshot msg_snapshot_snapshot_m;
};

struct msg_session_upd {
	struct hdr_r msg_session_upd_hdr_m;
	struct session msg_session_upd_session_m;
};

struct gone {
	uint32_t gone_uint0uint;
};

struct msg_gone {
	struct hdr_r msg_gone_hdr_m;
	struct gone msg_gone_gone_m;
};

struct attention_uint3tstr {
	struct zcbor_string attention_uint3tstr;
};

struct attention_uint4tstr {
	struct zcbor_string attention_uint4tstr;
};

struct attention_tstr_l_r {
	struct zcbor_string attention_tstr_l_tstr[4];
	size_t attention_tstr_l_tstr_count;
};

struct action_r {
	enum {
		action_approve_m_c = 0,
		action_deny_m_c = 1,
		action_focus_m_c = 2,
		action_reply_m_c = 3,
		action_ack_m_c = 4,
		action_dictate_m_c = 5,
	} action_choice;
};

struct attention_uint7uint {
	uint32_t attention_uint7uint;
};

struct attention_uint8uint {
	uint32_t attention_uint8uint;
};

struct attention_uint9uint {
	uint32_t attention_uint9uint;
};

struct attention_uint10uint {
	uint32_t attention_uint10uint;
};

struct attention_uint11uint {
	uint32_t attention_uint11uint;
};

struct attention {
	uint32_t attention_uint0uint;
	uint32_t attention_uint1uint;
	struct att_kind_r attention_att_kind_m;
	struct attention_uint3tstr attention_uint3tstr;
	bool attention_uint3tstr_present;
	struct attention_uint4tstr attention_uint4tstr;
	bool attention_uint4tstr_present;
	struct attention_tstr_l_r attention_tstr_l;
	bool attention_tstr_l_present;
	struct action_r attention_action_m_l_action_m[6];
	size_t attention_action_m_l_action_m_count;
	struct attention_uint7uint attention_uint7uint;
	bool attention_uint7uint_present;
	struct attention_uint8uint attention_uint8uint;
	bool attention_uint8uint_present;
	struct attention_uint9uint attention_uint9uint;
	bool attention_uint9uint_present;
	struct attention_uint10uint attention_uint10uint;
	bool attention_uint10uint_present;
	struct attention_uint11uint attention_uint11uint;
	bool attention_uint11uint_present;
};

struct msg_attention {
	struct hdr_r msg_attention_hdr_m;
	struct attention msg_attention_attention_m;
};

struct clear_why_r {
	enum {
		clear_why_answered_pc_m_c = 0,
		clear_why_answered_dev_m_c = 1,
		clear_why_expired_m_c = 2,
		clear_why_cancelled_m_c = 3,
		clear_why_handed_back_m_c = 4,
	} clear_why_choice;
};

struct clear {
	uint32_t clear_uint0uint;
	struct clear_why_r clear_why_m;
};

struct msg_clear {
	struct hdr_r msg_clear_hdr_m;
	struct clear msg_clear_clear_m;
};

struct cmd_op_r {
	enum {
		cmd_op_focus_m_c = 0,
		cmd_op_ack_m_c = 1,
		cmd_op_snooze_m_c = 2,
		cmd_op_approve_m_c = 3,
		cmd_op_deny_m_c = 4,
		cmd_op_reply_m_c = 5,
		cmd_op_cancel_m_c = 6,
		cmd_op_detail_m_c = 7,
		cmd_op_notify_done_m_c = 8,
		cmd_op_quiet_m_c = 9,
		cmd_op_scroll_m_c = 10,
		cmd_op_type_m_c = 11,
		cmd_op_dictate_m_c = 12,
		cmd_op_kb_pick_m_c = 13,
		cmd_op_kb_del_m_c = 14,
		cmd_op_kb_send_m_c = 15,
		cmd_op_kb_end_m_c = 16,
		cmd_op_rerun_m_c = 17,
	} cmd_op_choice;
};

struct cmd_uint1uint {
	uint32_t cmd_uint1uint;
};

struct cmd_uint2uint {
	uint32_t cmd_uint2uint;
};

struct scroll_unit_r {
	enum {
		scroll_unit_scroll_lines_m_c = 0,
		scroll_unit_scroll_pages_m_c = 1,
		scroll_unit_scroll_to_end_m_c = 2,
	} scroll_unit_choice;
};

struct scroll_arg_uint2uint {
	uint32_t scroll_arg_uint2uint;
};

struct scroll_arg {
	struct scroll_unit_r scroll_arg_scroll_unit_m;
	int32_t scroll_arg_uint1int;
	struct scroll_arg_uint2uint scroll_arg_uint2uint;
	bool scroll_arg_uint2uint_present;
};

struct cmd_arg_r {
	union {
		uint32_t cmd_arg_uint;
		struct zcbor_string cmd_arg_tstr;
		struct scroll_arg cmd_arg_scroll_arg_m;
	};
	enum {
		cmd_arg_uint_c,
		cmd_arg_tstr_c,
		cmd_arg_scroll_arg_m_c,
	} cmd_arg_choice;
};

struct cmd_arg_m {
	struct cmd_arg_r cmd_arg_m;
};

struct cmd_uint4uint {
	uint32_t cmd_uint4uint;
};

struct cmd {
	struct cmd_op_r cmd_op_m;
	struct cmd_uint1uint cmd_uint1uint;
	bool cmd_uint1uint_present;
	struct cmd_uint2uint cmd_uint2uint;
	bool cmd_uint2uint_present;
	struct cmd_arg_m cmd_arg_m;
	bool cmd_arg_m_present;
	struct cmd_uint4uint cmd_uint4uint;
	bool cmd_uint4uint_present;
};

struct msg_cmd {
	struct hdr_r msg_cmd_hdr_m;
	struct cmd msg_cmd_cmd_m;
};

struct result_err_r {
	enum {
		result_err_stale_m_c = 0,
		result_err_denied_m_c = 1,
		result_err_unsupported_m_c = 2,
		result_err_no_window_m_c = 3,
		result_err_timeout_m_c = 4,
		result_err_internal_m_c = 5,
	} result_err_choice;
};

struct result_err_m {
	struct result_err_r result_err_m;
};

struct result_uint2tstr {
	struct zcbor_string result_uint2tstr;
};

struct scroll_pos_uint2uint {
	uint32_t scroll_pos_uint2uint;
};

struct scroll_pos_uint3tstr {
	struct zcbor_string scroll_pos_uint3tstr;
};

struct scroll_pos {
	uint32_t scroll_pos_uint0uint;
	bool scroll_pos_uint1bool;
	struct scroll_pos_uint2uint scroll_pos_uint2uint;
	bool scroll_pos_uint2uint_present;
	struct scroll_pos_uint3tstr scroll_pos_uint3tstr;
	bool scroll_pos_uint3tstr_present;
};

struct result_scroll_pos_m {
	struct scroll_pos result_scroll_pos_m;
};

struct result {
	bool result_uint0bool;
	struct result_err_m result_err_m;
	bool result_err_m_present;
	struct result_uint2tstr result_uint2tstr;
	bool result_uint2tstr_present;
	struct result_scroll_pos_m result_scroll_pos_m;
	bool result_scroll_pos_m_present;
};

struct msg_result {
	struct hdr_r msg_result_hdr_m;
	struct result msg_result_result_m;
};

struct detail_uint0uint {
	uint32_t detail_uint0uint;
};

struct detail_uint1uint {
	uint32_t detail_uint1uint;
};

struct detail_uint3uint {
	uint32_t detail_uint3uint;
};

struct detail_uint4uint {
	uint32_t detail_uint4uint;
};

struct detail {
	struct detail_uint0uint detail_uint0uint;
	bool detail_uint0uint_present;
	struct detail_uint1uint detail_uint1uint;
	bool detail_uint1uint_present;
	struct zcbor_string detail_uint2tstr;
	struct detail_uint3uint detail_uint3uint;
	bool detail_uint3uint_present;
	struct detail_uint4uint detail_uint4uint;
	bool detail_uint4uint_present;
};

struct msg_detail {
	struct hdr_r msg_detail_hdr_m;
	struct detail msg_detail_detail_m;
};

struct msg_cfg {
	struct hdr_r msg_cfg_hdr_m;
	struct device_cfg msg_cfg_device_cfg_m;
};

struct batt {
	uint32_t batt_uint0uint;
	bool batt_uint1bool;
};

struct msg_batt {
	struct hdr_r msg_batt_hdr_m;
	struct batt msg_batt_batt_m;
};

struct msg_ping {
	struct hdr_r msg_ping_hdr_m;
};

struct msg_pong {
	struct hdr_r msg_pong_hdr_m;
};

struct log_level_r {
	enum {
		log_level_err_m_c = 0,
		log_level_warn_m_c = 1,
		log_level_info_m_c = 2,
		log_level_debug_m_c = 3,
	} log_level_choice;
};

struct log {
	struct log_level_r log_level_m;
	struct zcbor_string log_uint1tstr;
};

struct msg_log {
	struct hdr_r msg_log_hdr_m;
	struct log msg_log_log_m;
};

struct audio_codec_r {
	enum {
		audio_codec_adpcm16k_m_c = 0,
		audio_codec_opus16k_m_c = 1,
	} audio_codec_choice;
};

struct audio_start_uint2uint {
	uint32_t audio_start_uint2uint;
};

struct audio_start {
	uint32_t audio_start_uint0uint;
	struct audio_codec_r audio_start_audio_codec_m;
	struct audio_start_uint2uint audio_start_uint2uint;
	bool audio_start_uint2uint_present;
};

struct msg_audio_start {
	struct hdr_r msg_audio_start_hdr_m;
	struct audio_start msg_audio_start_audio_start_m;
};

struct audio {
	uint32_t audio_uint0uint;
	uint32_t audio_uint1uint;
	struct zcbor_string audio_uint2bstr;
};

struct msg_audio {
	struct hdr_r msg_audio_hdr_m;
	struct audio msg_audio_audio_m;
};

struct audio_end {
	uint32_t audio_end_uint0uint;
};

struct msg_audio_end {
	struct hdr_r msg_audio_end_hdr_m;
	struct audio_end msg_audio_end_audio_end_m;
};

struct transcript {
	uint32_t transcript_uint0uint;
	struct zcbor_string transcript_uint1tstr;
	uint32_t transcript_uint2uint;
};

struct msg_transcript {
	struct hdr_r msg_transcript_hdr_m;
	struct transcript msg_transcript_transcript_m;
};

struct kb_start {
	uint32_t kb_start_uint0uint;
	uint32_t kb_start_uint1uint;
	struct zcbor_string kb_start_uint2bstr;
};

struct msg_kb_start {
	struct hdr_r msg_kb_start_hdr_m;
	struct kb_start msg_kb_start_kb_start_m;
};

struct kb_stroke {
	uint32_t kb_stroke_uint0uint;
	uint32_t kb_stroke_uint1uint;
	struct zcbor_string kb_stroke_uint2bstr;
};

struct msg_kb_stroke {
	struct hdr_r msg_kb_stroke_hdr_m;
	struct kb_stroke msg_kb_stroke_kb_stroke_m;
};

struct kb_cands_uint3uint {
	uint32_t kb_cands_uint3uint;
};

struct kb_cands_result_err_m {
	struct result_err_r kb_cands_result_err_m;
};

struct kb_cands {
	uint32_t kb_cands_uint0uint;
	uint32_t kb_cands_uint1uint;
	struct zcbor_string kb_cands_tstr_l_tstr[4];
	size_t kb_cands_tstr_l_tstr_count;
	struct kb_cands_uint3uint kb_cands_uint3uint;
	bool kb_cands_uint3uint_present;
	struct kb_cands_result_err_m kb_cands_result_err_m;
	bool kb_cands_result_err_m_present;
};

struct msg_kb_cands {
	struct hdr_r msg_kb_cands_hdr_m;
	struct kb_cands msg_kb_cands_kb_cands_m;
};

struct usage_uint1uint {
	uint32_t usage_uint1uint;
};

struct usage_uint2uint {
	uint32_t usage_uint2uint;
};

struct usage_uint3uint {
	uint32_t usage_uint3uint;
};

struct usage_uint4uint {
	uint32_t usage_uint4uint;
};

struct usage_uint5uint {
	uint32_t usage_uint5uint;
};

struct usage_uint6uint {
	uint32_t usage_uint6uint;
};

struct usage {
	uint32_t usage_uint0uint;
	struct usage_uint1uint usage_uint1uint;
	bool usage_uint1uint_present;
	struct usage_uint2uint usage_uint2uint;
	bool usage_uint2uint_present;
	struct usage_uint3uint usage_uint3uint;
	bool usage_uint3uint_present;
	struct usage_uint4uint usage_uint4uint;
	bool usage_uint4uint_present;
	struct usage_uint5uint usage_uint5uint;
	bool usage_uint5uint_present;
	struct usage_uint6uint usage_uint6uint;
	bool usage_uint6uint_present;
};

struct msg_usage {
	struct hdr_r msg_usage_hdr_m;
	struct usage msg_usage_usage_m;
};

struct msg_dbg_ui_state_req {
	struct hdr_r msg_dbg_ui_state_req_hdr_m;
};

struct dbg_ui_state_uint1uint {
	uint32_t dbg_ui_state_uint1uint;
};

struct dbg_ui_state_uint2uint {
	uint32_t dbg_ui_state_uint2uint;
};

struct dbg_ui_state_uint5uint {
	uint32_t dbg_ui_state_uint5uint;
};

struct dbg_ui_state {
	struct zcbor_string dbg_ui_state_uint0tstr;
	struct dbg_ui_state_uint1uint dbg_ui_state_uint1uint;
	bool dbg_ui_state_uint1uint_present;
	struct dbg_ui_state_uint2uint dbg_ui_state_uint2uint;
	bool dbg_ui_state_uint2uint_present;
	struct zcbor_string dbg_ui_state_tstr_l_tstr[8];
	size_t dbg_ui_state_tstr_l_tstr_count;
	struct zcbor_string dbg_ui_state_uint4tstr;
	struct dbg_ui_state_uint5uint dbg_ui_state_uint5uint;
	bool dbg_ui_state_uint5uint_present;
};

struct msg_dbg_ui_state {
	struct hdr_r msg_dbg_ui_state_hdr_m;
	struct dbg_ui_state msg_dbg_ui_state_dbg_ui_state_m;
};

struct msg_dbg_screenshot_req {
	struct hdr_r msg_dbg_screenshot_req_hdr_m;
};

struct dbg_screenshot_chunk {
	uint32_t dbg_screenshot_chunk_uint0uint;
	uint32_t dbg_screenshot_chunk_uint1uint;
	uint32_t dbg_screenshot_chunk_uint2uint;
	struct zcbor_string dbg_screenshot_chunk_uint3bstr;
	bool dbg_screenshot_chunk_uint4bool;
};

struct msg_dbg_screenshot_chunk {
	struct hdr_r msg_dbg_screenshot_chunk_hdr_m;
	struct dbg_screenshot_chunk msg_dbg_screenshot_chunk_dbg_screenshot_chunk_m;
};

struct input_event_r {
	enum {
		input_event_tap_m_c = 0,
		input_event_double_tap_m_c = 1,
		input_event_long_press_m_c = 2,
		input_event_swipe_up_m_c = 3,
		input_event_swipe_down_m_c = 4,
		input_event_swipe_left_m_c = 5,
		input_event_swipe_right_m_c = 6,
		input_event_btn_a_m_c = 7,
		input_event_btn_b_m_c = 8,
		input_event_btn_a_long_m_c = 9,
		input_event_btn_b_long_m_c = 10,
		input_event_lift_m_c = 11,
		input_event_shake_m_c = 12,
	} input_event_choice;
};

struct dbg_inject_input_uint1int {
	int32_t dbg_inject_input_uint1int;
};

struct dbg_inject_input_uint2int {
	int32_t dbg_inject_input_uint2int;
};

struct dbg_inject_input {
	struct input_event_r dbg_inject_input_input_event_m;
	struct dbg_inject_input_uint1int dbg_inject_input_uint1int;
	bool dbg_inject_input_uint1int_present;
	struct dbg_inject_input_uint2int dbg_inject_input_uint2int;
	bool dbg_inject_input_uint2int_present;
};

struct msg_dbg_inject_input {
	struct hdr_r msg_dbg_inject_input_hdr_m;
	struct dbg_inject_input msg_dbg_inject_input_dbg_inject_input_m;
};

struct dbg_set_log_level {
	struct log_level_r dbg_set_log_level_log_level_m;
};

struct msg_dbg_set_log_level {
	struct hdr_r msg_dbg_set_log_level_hdr_m;
	struct dbg_set_log_level msg_dbg_set_log_level_dbg_set_log_level_m;
};

struct envelope_r {
	union {
		struct msg_hello envelope_msg_hello_m;
		struct msg_welcome envelope_msg_welcome_m;
		struct msg_snapshot envelope_msg_snapshot_m;
		struct msg_session_upd envelope_msg_session_upd_m;
		struct msg_gone envelope_msg_gone_m;
		struct msg_attention envelope_msg_attention_m;
		struct msg_clear envelope_msg_clear_m;
		struct msg_cmd envelope_msg_cmd_m;
		struct msg_result envelope_msg_result_m;
		struct msg_detail envelope_msg_detail_m;
		struct msg_cfg envelope_msg_cfg_m;
		struct msg_batt envelope_msg_batt_m;
		struct msg_ping envelope_msg_ping_m;
		struct msg_pong envelope_msg_pong_m;
		struct msg_log envelope_msg_log_m;
		struct msg_audio_start envelope_msg_audio_start_m;
		struct msg_audio envelope_msg_audio_m;
		struct msg_audio_end envelope_msg_audio_end_m;
		struct msg_transcript envelope_msg_transcript_m;
		struct msg_kb_start envelope_msg_kb_start_m;
		struct msg_kb_stroke envelope_msg_kb_stroke_m;
		struct msg_kb_cands envelope_msg_kb_cands_m;
		struct msg_usage envelope_msg_usage_m;
		struct msg_dbg_ui_state_req envelope_msg_dbg_ui_state_req_m;
		struct msg_dbg_ui_state envelope_msg_dbg_ui_state_m;
		struct msg_dbg_screenshot_req envelope_msg_dbg_screenshot_req_m;
		struct msg_dbg_screenshot_chunk envelope_msg_dbg_screenshot_chunk_m;
		struct msg_dbg_inject_input envelope_msg_dbg_inject_input_m;
		struct msg_dbg_set_log_level envelope_msg_dbg_set_log_level_m;
	};
	enum {
		envelope_msg_hello_m_c,
		envelope_msg_welcome_m_c,
		envelope_msg_snapshot_m_c,
		envelope_msg_session_upd_m_c,
		envelope_msg_gone_m_c,
		envelope_msg_attention_m_c,
		envelope_msg_clear_m_c,
		envelope_msg_cmd_m_c,
		envelope_msg_result_m_c,
		envelope_msg_detail_m_c,
		envelope_msg_cfg_m_c,
		envelope_msg_batt_m_c,
		envelope_msg_ping_m_c,
		envelope_msg_pong_m_c,
		envelope_msg_log_m_c,
		envelope_msg_audio_start_m_c,
		envelope_msg_audio_m_c,
		envelope_msg_audio_end_m_c,
		envelope_msg_transcript_m_c,
		envelope_msg_kb_start_m_c,
		envelope_msg_kb_stroke_m_c,
		envelope_msg_kb_cands_m_c,
		envelope_msg_usage_m_c,
		envelope_msg_dbg_ui_state_req_m_c,
		envelope_msg_dbg_ui_state_m_c,
		envelope_msg_dbg_screenshot_req_m_c,
		envelope_msg_dbg_screenshot_chunk_m_c,
		envelope_msg_dbg_inject_input_m_c,
		envelope_msg_dbg_set_log_level_m_c,
	} envelope_choice;
};

#ifdef __cplusplus
}
#endif

#endif /* AGENTLINK_CODEC_TYPES_H__ */
