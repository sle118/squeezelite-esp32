/*
 *
 *      Sebastien L. 2023, sle118@hotmail.com
 *      Philippe G. 2023, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 *  License Overview:
 *  ----------------
 *  The MIT License is a permissive open source license. As a user of this software, you are free to:
 *  - Use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of this software.
 *  - Use the software for private, commercial, or any other purposes.
 *
 *  Conditions:
 *  - You must include the above copyright notice and this permission notice in all
 *    copies or substantial portions of the Software.
 *
 *  The MIT License offers a high degree of freedom and is well-suited for both open source and
 *  commercial applications. It places minimal restrictions on how the software can be used,
 *  modified, and redistributed. For more details on the MIT License, please refer to the link above.
 */

#pragma once
#include "esp_partition.h"
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t bootstate_read_counter(void);

/**
 * @fn uint32_t bootstate_uptate_counter(int32_t xValue)
 * Updates the boot state counter.
 *
 * Increments the RebootCounter by 'xValue'. If 'xValue' is 0, or if RebootCounter
 * exceeds 100, it resets both RebootCounter and RecoveryRebootCounter to 0.
 * Additionally, updates the RecoveryRebootCounter based on the recovery state.
 *
 * @param xValue The value to increment the RebootCounter.
 * @return The updated value of the RebootCounter.
 */
uint32_t bootstate_uptate_counter(int32_t xValue);

/**
 * @fn void bootstate_handle_boot(void)
 * Handles the boot state logic on system startup.
 *
 * This function manages boot states based on cold boot indicators, recovery states, and reboot counters.
 * It includes logic for handling regular and recovery boots, and manages the boot counter's logic
 * for different scenarios such as factory resets or configuration erasures.
 */
void bootstate_handle_boot(void);

/**
 * @fn esp_err_t guided_boot(esp_partition_subtype_t partition_subtype)
 * Performs a guided boot to a specified partition subtype.
 *
 * This function attempts to reboot the device to a specified partition subtype.
 * It includes logic to handle different scenarios based on the current running state
 * (normal or recovery) and the target partition subtype.
 *
 * @param partition_subtype The target partition subtype to boot to.
 * @return ESP_OK on successful execution, or an ESP_ERR code on failure.
 */
esp_err_t guided_boot(esp_partition_subtype_t partition_subtype);

/**
 * @fn esp_err_t guided_restart_ota(void)
 * Initiates a guided restart to an OTA (Over The Air) update partition.
 *
 * This function logs the intention to boot to the Squeezelite OTA partition and then
 * calls guided_boot to execute the reboot. It always returns ESP_FAIL as the system
 * is expected to reboot and not return from the function.
 *
 * @return ESP_FAIL to indicate the function should not return as a reboot is expected.
 */
esp_err_t guided_restart_ota(void);

/**
 * @fn esp_err_t guided_factory(void)
 * Initiates a guided restart to the factory partition.
 *
 * This function logs the intention to boot to the recovery partition and then
 * calls guided_boot to execute the reboot. It always returns ESP_FAIL as the system
 * is expected to reboot and not return from the function.
 *
 * @return ESP_FAIL to indicate the function should not return as a reboot is expected.
 */
esp_err_t guided_factory(void);

/**
 * @fn void simple_restart(void)
 * Performs a simple system restart.
 *
 * This function logs a reboot message, attempts to commit any pending configurations,
 * waits for a short duration, and then triggers a system restart using `esp_restart`.
 */
void simple_restart(void);

/**
 * @fn bool is_restarting(void)
 * Checks if the system is in the process of restarting.
 *
 * @return `true` if the system is currently restarting, `false` otherwise.
 */
bool is_restarting(void);

/**
 * @var extern bool is_recovery_running
 * Indicates whether the system is currently running in recovery mode.
 *
 * This variable is used to determine the current operating mode of the system.
 * It should be set to `true` when the system is operating in recovery mode, and
 * `false` otherwise. As an external variable, it is likely defined and managed
 * in another file/module.
 */
extern bool is_recovery_running;

#ifdef __cplusplus
}
#endif
