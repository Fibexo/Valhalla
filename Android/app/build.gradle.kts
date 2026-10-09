plugins {
    id("com.android.application")
}

val vcpkgRoot = System.getenv("VCPKG_ROOT")?.replace('\\', '/')
val androidHome = System.getenv("ANDROID_HOME")?.replace('\\', '/')
val ndkVersionName = "30.0.16248370"
val vcpkgToolchain = "$vcpkgRoot/scripts/buildsystems/vcpkg.cmake"
val ndkToolchain = "$androidHome/ndk/$ndkVersionName/build/cmake/android.toolchain.cmake"

android {
    namespace = "com.fibexo.valhalla"
    compileSdk {
        version = release(37)
    }
    ndkVersion = ndkVersionName


    defaultConfig {
        applicationId = "com.fibexo.valhalla"
        minSdk = 28
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain",
                    "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$ndkToolchain",
                    "-DVCPKG_TARGET_TRIPLET=arm64-android-dynamic",
                    "-DANDROID_PLATFORM=android-37",
                    "-DANDROID_STL=c++_shared",
                )
                abiFilters += "arm64-v8a"
            }
        }
    }
    buildFeatures { prefab = true }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "4.1.2+"
        }
    }
}

dependencies {
    implementation(files("libs/SDL3-3.4.18.aar"))
    implementation("androidx.core:core-ktx:1.19.1")
    implementation("androidx.appcompat:appcompat:1.8.0")
    implementation("com.google.android.material:material:1.14.0")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.7.0")

}