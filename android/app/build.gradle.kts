plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.deltaradar.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.deltaradar.app"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")
    testImplementation("junit:junit:4.13.2")
}

// Ship the reader with the APK so launcher and native protocol stay in sync.
val scannerAssets = layout.buildDirectory.dir("generated/scannerAssets")
val prepareScannerAssets by tasks.registering(Copy::class) {
    from(rootProject.projectDir.parentFile.resolve("scanner/memscan"))
    rename { "ue_telemetry" }
    into(scannerAssets)
}
android.sourceSets.getByName("main").assets.srcDir(scannerAssets)
tasks.named("preBuild") { dependsOn(prepareScannerAssets) }

