import { PermissionsAndroid, Platform } from 'react-native';

/**
 * Requests whatever runtime permissions BLE scanning needs on this Android
 * API level:
 *   - API 31+ (Android 12+): BLUETOOTH_SCAN + BLUETOOTH_CONNECT
 *     (BLUETOOTH_SCAN is declared with `neverForLocation` in the manifest,
 *     so it does not also require location permission/services).
 *   - API < 31: ACCESS_FINE_LOCATION (BLE scan results were treated as
 *     location-derived data pre-Android-12).
 *
 * Returns true iff all required permissions were granted.
 */
export async function requestBlePermissions(): Promise<boolean> {
  if (Platform.OS !== 'android') {
    // Out of scope for this app (Android-only), but don't block iOS from
    // at least attempting to run.
    return true;
  }

  const apiLevel = Platform.Version as number;

  if (apiLevel >= 31) {
    const results = await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
    ]);
    return (
      results[PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN] ===
        PermissionsAndroid.RESULTS.GRANTED &&
      results[PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT] ===
        PermissionsAndroid.RESULTS.GRANTED
    );
  }

  const granted = await PermissionsAndroid.request(
    PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
  );
  return granted === PermissionsAndroid.RESULTS.GRANTED;
}
