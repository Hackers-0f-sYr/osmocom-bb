#pragma once

/*! \addtogroup auth
 *  @{
 */

/*! \file auth.h */

#include <stdint.h>

#include <osmocom/core/linuxlist.h>

/*! \brief Authentication Type (GSM/UMTS) */
enum osmo_sub_auth_type {
	OSMO_AUTH_TYPE_NONE	= 0x00,
	OSMO_AUTH_TYPE_GSM	= 0x01,
	OSMO_AUTH_TYPE_UMTS	= 0x02,
};

/*! \brief Authentication Algorithm */
enum osmo_auth_algo {
	OSMO_AUTH_ALG_NONE,
	OSMO_AUTH_ALG_COMP128v1,
	OSMO_AUTH_ALG_COMP128v2,
	OSMO_AUTH_ALG_COMP128v3,
	OSMO_AUTH_ALG_XOR,
	OSMO_AUTH_ALG_MILENAGE,
	_OSMO_AUTH_ALG_NUM,
};

/*! \brief permanent (secret) subscriber auth data */
struct osmo_sub_auth_data {
	enum osmo_sub_auth_type type;
	enum osmo_auth_algo algo;
	union {
		struct {
			uint8_t opc[16]; /*!< operator invariant value */
			uint8_t k[16];	/*!< secret key of the subscriber */
			uint8_t amf[2];
			uint64_t sqn;	/*!< sequence number */
			int opc_is_op;	/*!< is the OPC field OPC (0) or OP (1) ? */
		} umts;
		struct {
			uint8_t ki[16];	/*!< secret key */
		} gsm;
	} u;
};

/* data structure describing a computed auth vector, generated by AuC */
struct osmo_auth_vector {
	uint8_t rand[16];	/*!< random challenge */
	uint8_t autn[16];	/*!< authentication nonce */
	uint8_t ck[16];		/*!< ciphering key */
	uint8_t ik[16];		/*!< integrity key */
	uint8_t res[16];	/*!< authentication result */
	uint8_t res_len;	/*!< length (in bytes) of res */
	uint8_t kc[8];		/*!< Kc for GSM encryption (A5) */
	uint8_t sres[4];	/*!< authentication result for GSM */
	uint32_t auth_types;	/*!< bitmask of OSMO_AUTH_TYPE_* */
};

/* \brief An implementation of an authentication algorithm */
struct osmo_auth_impl {
	struct llist_head list;
	enum osmo_auth_algo algo; /*!< algorithm we implement */
	const char *name;	/*!< name of the implementation */
	unsigned int priority;	/*!< priority value (resp. othe implementations */

	/*! \brief callback for generate authentication vectors */
	int (*gen_vec)(struct osmo_auth_vector *vec,
			struct osmo_sub_auth_data *aud,
			const uint8_t *_rand);

	/* \brief callback for generationg auth vectors + re-sync */
	int (*gen_vec_auts)(struct osmo_auth_vector *vec,
			    struct osmo_sub_auth_data *aud,
			    const uint8_t *rand_auts, const uint8_t *auts,
			    const uint8_t *_rand);
};

int osmo_auth_gen_vec(struct osmo_auth_vector *vec,
		      struct osmo_sub_auth_data *aud, const uint8_t *_rand);

int osmo_auth_gen_vec_auts(struct osmo_auth_vector *vec,
			   struct osmo_sub_auth_data *aud,
			   const uint8_t *rand_auts, const uint8_t *auts,
			   const uint8_t *_rand);

int osmo_auth_register(struct osmo_auth_impl *impl);

int osmo_auth_load(const char *path);

int osmo_auth_supported(enum osmo_auth_algo algo);
void osmo_c4(uint8_t *ck, const uint8_t *kc);
const char *osmo_auth_alg_name(enum osmo_auth_algo alg);
enum osmo_auth_algo osmo_auth_alg_parse(const char *name);

/* @} */
