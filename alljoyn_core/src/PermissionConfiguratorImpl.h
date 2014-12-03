#ifndef _ALLJOYN_PERMISSION_CONFIGURATOR_IMPL_H
#define _ALLJOYN_PERMISSION_CONFIGURATOR_IMPL_H
/**
 * @file
 * This file defines the Permission Configurator class that exposes some permission
 * management capabilities to the application.
 */

/******************************************************************************
 * Copyright (c) 2014, AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#ifndef __cplusplus
#error Only include PermissionConfiguratorImpl.h in C++ code.
#endif

#include <alljoyn/PermissionConfigurator.h>

namespace ajn {

/**
 * Class to allow the application to manage some limited permission feature.
 */

class PermissionConfiguratorImpl : public PermissionConfigurator {

  public:

    /**
     * Constructor
     *
     */
    PermissionConfiguratorImpl(BusAttachment& bus) : PermissionConfigurator(), bus(bus)
    {
    }

    /**
     * virtual destructor
     */
    virtual ~PermissionConfiguratorImpl()
    {
    }

    /**
     * Set the permission manifest for the application.
     * @params rules the permission rules.
     * @params count the number of permission rules
     * @return ER_OK if successful; otherwise, an error code.
     */
    QStatus SetPermissionManifest(PermissionPolicy::Rule* rules, size_t count);

    /**
     * Retrieve the claimable state of the application.
     * @return the claimable state
     */
    ClaimableState GetClaimableState();

    /**
     * Set the claimable state to be claimable or not.  The resulting claimable      * state would be either STATE_UNCLAIMABLE or STATE_CLAIMABLE depending on
     * the value of the input flag.  This action is not allowed when the current
     * state is STATE_CLAIMED.
     * @param claimable flag
     * @return
     *      - #ER_OK if action is allowed.
     *      - #ER_INVALID_CLAIMABLE_STATE if current state is STATE_CLAIMED
     */
    QStatus SetClaimable(bool claimable);

    /**
     * Generate the signing key pair and store it in the key store.
     * @return ER_OK if successful; otherwise, an error code.
     */
    QStatus GenerateSigningKeyPair();

    /**
     * Retrieve the public key info fo the signing key.
     * @param[out] the public key info
     * @return ER_OK if successful; otherwise, an error code.
     */
    QStatus GetSigningPublicKey(qcc::KeyInfoECC& keyInfo);

    /**
     * Sign the X509 certificate using the signing key
     * @param[out] the certificate to be signed
     * @return ER_OK if successful; otherwise, an error code.
     */
    QStatus SignCertificate(qcc::CertificateX509& cert);

    /**
     * Reset the Permission module by removing all the trust anchors, DSA keys,
     * installed policy and certificates.
     * @return ER_OK if successful; otherwise, an error code.
     */
    QStatus Reset();

  private:
    BusAttachment& bus;
};

}
#endif
