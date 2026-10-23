// SPDX-License-Identifier: Apache-2.0
// Self-test entry points of the fingerprint sensors this device does not have.
// libbauthserver only calls the one for the sensor it detected (Egis here), so
// these just report failure.

int gdxFpSensorTest(void) { return -1; }
int qbtFpSensorTest(void) { return -1; }
int synaFpSensorTest(void) { return -1; }
