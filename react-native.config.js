/**
 * Autolinking configuration for a pure C++ Turbo Module (no Java/Kotlin,
 * no gradle project). Requires React Native >= 0.76.
 *
 * @type {import('@react-native-community/cli-types').UserDependencyConfig}
 */
module.exports = {
  dependency: {
    platforms: {
      android: {
        cmakeListsPath: "generated/jni/CMakeLists.txt",
        cxxModuleCMakeListsModuleName: "polygonclipping",
        cxxModuleCMakeListsPath: "CMakeLists.txt",
        cxxModuleHeaderName: "PolygonClippingImpl",
      },
    },
  },
};
