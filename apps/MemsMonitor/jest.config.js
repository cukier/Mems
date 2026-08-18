module.exports = {
  preset: '@react-native/jest-preset',
  // react-native-ble-plx ships untranspiled ESM source; let Jest's Babel
  // transform run over it instead of leaving it in the default
  // node_modules ignore list.
  transformIgnorePatterns: [
    'node_modules/(?!(?:react-native|@react-native|react-native-ble-plx)/)',
  ],
};
