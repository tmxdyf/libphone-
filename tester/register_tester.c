/*
	belle-sip - SIP (RFC3261) library.
    Copyright (C) 2010  Belledonne Communications SARL

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "CUnit/Basic.h"
#include "linphonecore.h"
#include "private.h"
#include "liblinphone_tester.h"



static LinphoneCore* create_lc() {
	return create_lc_with_auth(0);
}

void registration_state_changed(struct _LinphoneCore *lc, LinphoneProxyConfig *cfg, LinphoneRegistrationState cstate, const char *message){
		stats* counters;
		ms_message("New registration state %s for user id [%s] at proxy [%s]\n"
				,linphone_registration_state_to_string(cstate)
				,linphone_proxy_config_get_identity(cfg)
				,linphone_proxy_config_get_addr(cfg));
		counters = (stats*)linphone_core_get_user_data(lc);
		switch (cstate) {
		case LinphoneRegistrationNone:counters->number_of_LinphoneRegistrationNone++;break;
		case LinphoneRegistrationProgress:counters->number_of_LinphoneRegistrationProgress++;break;
		case LinphoneRegistrationOk:counters->number_of_LinphoneRegistrationOk++;break;
		case LinphoneRegistrationCleared:counters->number_of_LinphoneRegistrationCleared++;break;
		case LinphoneRegistrationFailed:counters->number_of_LinphoneRegistrationFailed++;break;
		default:
			CU_FAIL("unexpected event");break;
		}

}

static void register_with_refresh_base_2(LinphoneCore* lc, bool_t refresh,const char* domain,const char* route,bool_t late_auth_info,LCSipTransports transport) {
	int retry=0;
	char* addr;
	LinphoneProxyConfig* proxy_cfg;
	stats* counters;
	LinphoneAddress *from;
	const char* server_addr;
	LinphoneAuthInfo *info;

	CU_ASSERT_PTR_NOT_NULL(lc);
	if (!lc) return;
	counters = (stats*)linphone_core_get_user_data(lc);
	reset_counters(counters);
	linphone_core_set_sip_transports(lc,&transport);

	proxy_cfg = linphone_proxy_config_new();

	from = create_linphone_address(domain);

	linphone_proxy_config_set_identity(proxy_cfg,addr=linphone_address_as_string(from));
	ms_free(addr);
	server_addr = linphone_address_get_domain(from);

	linphone_proxy_config_enable_register(proxy_cfg,TRUE);
	linphone_proxy_config_expires(proxy_cfg,1);
	if (route) {
		linphone_proxy_config_set_route(proxy_cfg,route);
		linphone_proxy_config_set_server_addr(proxy_cfg,route);
	} else {
		linphone_proxy_config_set_server_addr(proxy_cfg,server_addr);
	}
	linphone_address_destroy(from);

	linphone_core_add_proxy_config(lc,proxy_cfg);
	linphone_core_set_default_proxy(lc,proxy_cfg);

	while (counters->number_of_LinphoneRegistrationOk<1+(refresh!=0) && retry++ <310) {
		linphone_core_iterate(lc);
		if (counters->number_of_auth_info_requested>0 && late_auth_info) {
			info=linphone_auth_info_new(test_username,NULL,test_password,NULL,auth_domain); /*create authentication structure from identity*/
			linphone_core_add_auth_info(lc,info); /*add authentication info to LinphoneCore*/
		}
		ms_usleep(100000);
	}
	CU_ASSERT_TRUE(linphone_proxy_config_is_registered(proxy_cfg));
	CU_ASSERT_EQUAL(counters->number_of_LinphoneRegistrationNone,0);
	CU_ASSERT_EQUAL(counters->number_of_LinphoneRegistrationProgress,1);
	CU_ASSERT_EQUAL(counters->number_of_LinphoneRegistrationOk,1+(refresh!=0));
	CU_ASSERT_EQUAL(counters->number_of_LinphoneRegistrationFailed,late_auth_info?1:0);
	CU_ASSERT_EQUAL(counters->number_of_LinphoneRegistrationCleared,0);

}

static void register_with_refresh_base(LinphoneCore* lc, bool_t refresh,const char* domain,const char* route) {
	LCSipTransports transport = {5070,5070,0,5071};
	register_with_refresh_base_2(lc,refresh,domain,route,FALSE,transport);
}

static void register_with_refresh(LinphoneCore* lc, bool_t refresh,const char* domain,const char* route) {
	stats* counters = (stats*)linphone_core_get_user_data(lc);
	register_with_refresh_base(lc,refresh,domain,route);
	linphone_core_destroy(lc);
	CU_ASSERT_EQUAL(counters->number_of_LinphoneRegistrationCleared,1);


}

static void register_with_refresh_with_send_error() {
	int retry=0;
	LinphoneCore* lc = create_lc_with_auth(1);
	stats* counters = (stats*)linphone_core_get_user_data(lc);
	LinphoneAuthInfo *info=linphone_auth_info_new(test_username,NULL,test_password,NULL,auth_domain); /*create authentication structure from identity*/
	char route[256];
	sprintf(route,"sip:%s",test_route);
	linphone_core_add_auth_info(lc,info); /*add authentication info to LinphoneCore*/

	register_with_refresh_base(lc,TRUE,auth_domain,route);
	/*simultate a network error*/
	sal_set_send_error(lc->sal, -1);
	while (counters->number_of_LinphoneRegistrationFailed<1 && retry++ <20) {
			linphone_core_iterate(lc);
			ms_usleep(100000);
	}
	CU_ASSERT_EQUAL(counters->number_of_LinphoneRegistrationFailed,1);
	linphone_core_destroy(lc);

	CU_ASSERT_EQUAL(counters->number_of_LinphoneRegistrationCleared,0);

}

static void simple_register(){
	LinphoneCore* lc = create_lc();
	stats* counters = (stats*)linphone_core_get_user_data(lc);
	register_with_refresh(lc,FALSE,NULL,NULL);
	CU_ASSERT_EQUAL(counters->number_of_auth_info_requested,0);
}

/*take care of min expires configuration from server*/
static void simple_register_with_refresh() {
	LinphoneCore* lc = create_lc();
	stats* counters = (stats*)linphone_core_get_user_data(lc);
	register_with_refresh(lc,TRUE,NULL,NULL);
	CU_ASSERT_EQUAL(counters->number_of_auth_info_requested,0);
}

static void simple_auth_register_with_refresh() {
	LinphoneCore* lc = create_lc_with_auth(1);
	stats* counters = (stats*)linphone_core_get_user_data(lc);
	char route[256];
	sprintf(route,"sip:%s",test_route);
	register_with_refresh(lc,TRUE,auth_domain,route);
	CU_ASSERT_EQUAL(counters->number_of_auth_info_requested,1);
}

static void simple_tcp_register(){
	char route[256];
	LinphoneCore* lc;
	sprintf(route,"sip:%s;transport=tcp",test_route);
	lc = create_lc();
	register_with_refresh(lc,FALSE,test_domain,route);
}

static void simple_tcp_register_compatibility_mode(){
	char route[256];
	LinphoneCore* lc;
	LCSipTransports transport = {0,5070,0,0};
	sprintf(route,"sip:%s",test_route);
	lc = create_lc();
	register_with_refresh_base_2(lc,FALSE,test_domain,route,FALSE,transport);
}


static void simple_tls_register(){
	char route[256];
	LinphoneCore* lc;
	sprintf(route,"sip:%s;transport=tls",test_route);
	lc = create_lc();
	register_with_refresh(lc,FALSE,test_domain,route);
}

static void simple_authenticated_register(){
	stats* counters;
	LinphoneCore* lc = create_lc();
	LinphoneAuthInfo *info=linphone_auth_info_new(test_username,NULL,test_password,NULL,auth_domain); /*create authentication structure from identity*/
	char route[256];
	sprintf(route,"sip:%s",test_route);
	linphone_core_add_auth_info(lc,info); /*add authentication info to LinphoneCore*/
	counters = (stats*)linphone_core_get_user_data(lc);
	register_with_refresh(lc,FALSE,auth_domain,route);
	CU_ASSERT_EQUAL(counters->number_of_auth_info_requested,0);
}

static void ha1_authenticated_register(){
	stats* counters;
	LinphoneCore* lc = create_lc();
	char ha1[33];
	LinphoneAuthInfo *info;
	char route[256];
	sal_auth_compute_ha1(test_username,auth_domain,test_password,ha1);
	info=linphone_auth_info_new(test_username,NULL,NULL,ha1,auth_domain); /*create authentication structure from identity*/
	sprintf(route,"sip:%s",test_route);
	linphone_core_add_auth_info(lc,info); /*add authentication info to LinphoneCore*/
	counters = (stats*)linphone_core_get_user_data(lc);
	register_with_refresh(lc,FALSE,auth_domain,route);
	CU_ASSERT_EQUAL(counters->number_of_auth_info_requested,0);
}

static void authenticated_register_with_no_initial_credentials(){
	LinphoneCoreVTable v_table;
	LinphoneCore* lc;
	stats stat;
	stats* counters;
	char route[256];
	sprintf(route,"sip:%s",test_route);
	memset (&v_table,0,sizeof(v_table));
	v_table.registration_state_changed=registration_state_changed;
	v_table.auth_info_requested=auth_info_requested;
	lc = linphone_core_new(&v_table,NULL,NULL,NULL);
	linphone_core_set_user_data(lc,&stat);
	counters= (stats*)linphone_core_get_user_data(lc);
	counters->number_of_auth_info_requested=0;
	register_with_refresh(lc,FALSE,auth_domain,route);
	CU_ASSERT_EQUAL(counters->number_of_auth_info_requested,1);
}

static void auth_info_requested2(LinphoneCore *lc, const char *realm, const char *username) {
	stats* counters;
	ms_message("Auth info requested  for user id [%s] at realm [%s]\n"
					,username
					,realm);
	counters = (stats*)linphone_core_get_user_data(lc);
	counters->number_of_auth_info_requested++;
}

static void authenticated_register_with_late_credentials(){
	LinphoneCoreVTable v_table;
	LinphoneCore* lc;
	stats stat;
	stats* counters;
	LCSipTransports transport = {5070,5070,0,5071};
	char route[256];
	sprintf(route,"sip:%s",test_route);
	memset (&v_table,0,sizeof(v_table));
	v_table.registration_state_changed=registration_state_changed;
	v_table.auth_info_requested=auth_info_requested2;
	lc =  linphone_core_new(&v_table,NULL,NULL,NULL);
	linphone_core_set_user_data(lc,&stat);
	counters = (stats*)linphone_core_get_user_data(lc);
	register_with_refresh_base_2(lc,FALSE,auth_domain,route,TRUE,transport);
	CU_ASSERT_EQUAL(counters->number_of_auth_info_requested,1);
	linphone_core_destroy(lc);
}

static LinphoneCore* configure_lc(LinphoneCoreVTable* v_table) {
	return configure_lc_from(v_table, liblinphone_tester_file_prefix, "multi_account_lrc", 3);
}

static void multiple_proxy(){
	LinphoneCoreVTable v_table;
	LinphoneCore* lc;
	memset (&v_table,0,sizeof(LinphoneCoreVTable));
	v_table.registration_state_changed=registration_state_changed;
	lc=configure_lc(&v_table);
	linphone_core_destroy(lc);
}

static void network_state_change(){
	LinphoneCoreVTable v_table;
	LinphoneCore* lc;
	int register_ok;
	stats* counters ;

	memset (&v_table,0,sizeof(LinphoneCoreVTable));
	v_table.registration_state_changed=registration_state_changed;
	lc=configure_lc(&v_table);
	counters = (stats*)linphone_core_get_user_data(lc);
	register_ok=counters->number_of_LinphoneRegistrationOk;
	linphone_core_set_network_reachable(lc,FALSE);
	CU_ASSERT_TRUE(wait_for(lc,lc,&counters->number_of_LinphoneRegistrationNone,register_ok));
	linphone_core_set_network_reachable(lc,TRUE);
	wait_for(lc,lc,&counters->number_of_LinphoneRegistrationOk,2*register_ok);
	linphone_core_destroy(lc);
}
static int get_number_of_udp_proxy(const LinphoneCore* lc) {
	int number_of_udp_proxy=0;
	LinphoneProxyConfig* proxy_cfg;
	MSList* proxys;
	for (proxys=(MSList*)linphone_core_get_proxy_config_list(lc);proxys!=NULL;proxys=proxys->next) {
			proxy_cfg=(LinphoneProxyConfig*)proxys->data;
			if (strcmp("udp",linphone_proxy_config_get_transport(proxy_cfg))==0)
				number_of_udp_proxy++;
	}
	return number_of_udp_proxy;
}
static void transport_change(){
	LinphoneCoreVTable v_table;
	LinphoneCore* lc;
	int register_ok;
	stats* counters ;
	LCSipTransports sip_tr;
	LCSipTransports sip_tr_orig;
	int number_of_udp_proxy=0;
	int total_number_of_proxies;
	memset(&sip_tr,0,sizeof(sip_tr));
	memset (&v_table,0,sizeof(LinphoneCoreVTable));
	v_table.registration_state_changed=registration_state_changed;
	lc=configure_lc(&v_table);
	counters = (stats*)linphone_core_get_user_data(lc);
	register_ok=counters->number_of_LinphoneRegistrationOk;

	number_of_udp_proxy=get_number_of_udp_proxy(lc);
	total_number_of_proxies=ms_list_size(linphone_core_get_proxy_config_list(lc));
	linphone_core_get_sip_transports(lc,&sip_tr_orig);

	sip_tr.udp_port=sip_tr_orig.udp_port;

	/*keep only udp*/
	linphone_core_set_sip_transports(lc,&sip_tr);
	CU_ASSERT_TRUE(wait_for(lc,lc,&counters->number_of_LinphoneRegistrationOk,register_ok+number_of_udp_proxy));

	CU_ASSERT_TRUE(wait_for(lc,lc,&counters->number_of_LinphoneRegistrationFailed,register_ok+(total_number_of_proxies-number_of_udp_proxy)));

	linphone_core_destroy(lc);
}

static void io_recv_error(){
	LinphoneCoreVTable v_table;
	LinphoneCore* lc;
	int register_ok;
	stats* counters ;
	int number_of_udp_proxy=0;

	memset (&v_table,0,sizeof(LinphoneCoreVTable));
	v_table.registration_state_changed=registration_state_changed;
	lc=configure_lc(&v_table);
	counters = (stats*)linphone_core_get_user_data(lc);
	register_ok=counters->number_of_LinphoneRegistrationOk;
	number_of_udp_proxy=get_number_of_udp_proxy(lc);
	sal_set_recv_error(lc->sal, 0);

	CU_ASSERT_TRUE(wait_for(lc,lc,&counters->number_of_LinphoneRegistrationFailed,register_ok-number_of_udp_proxy /*because 1 udp*/));
	sal_set_recv_error(lc->sal, 1); /*reset*/

	linphone_core_destroy(lc);
}


test_t register_tests[] = {
	{ "Simple register", simple_register },
	{ "TCP register", simple_tcp_register },
	{ "TCP register compatibility mode", simple_tcp_register_compatibility_mode },
	{ "TLS register", simple_tls_register },
	{ "Simple authenticated register", simple_authenticated_register },
	{ "Ha1 authenticated register", ha1_authenticated_register },
	{ "Digest auth without initial credentials", authenticated_register_with_no_initial_credentials },
	{ "Authenticated register with late credentials", authenticated_register_with_late_credentials },
	{ "Register with refresh", simple_register_with_refresh },
	{ "Authenticated register with refresh", simple_auth_register_with_refresh },
	{ "Register with refresh and send error", register_with_refresh_with_send_error },
	{ "Multi account", multiple_proxy },
	{ "Transport change", transport_change },
	{ "Network state change", network_state_change },
	{ "io_recv_error_0", io_recv_error }
};

test_suite_t register_test_suite = {
	"Register",
	NULL,
	NULL,
	sizeof(register_tests) / sizeof(register_tests[0]),
	register_tests
};
