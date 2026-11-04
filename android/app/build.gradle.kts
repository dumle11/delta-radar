import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val scannerDeployment = Properties().apply {
    // Candidate builds can target their reviewed manifest without activating a
    // new scanner in the ordinary local build or changing the deployment pin.
    val path = providers.gradleProperty("scannerDeploymentFile")
        .getOrElse("../scanner/deployment.properties")
    rootProject.file(path).inputStream().use { load(it) }
}

android {
    buildFeatures { buildConfig = true }
    namespace = "com.deltaradar.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.deltaradar.app"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        for (key in listOf("path", "sha256", "revision", "sourceSha256")) {
            buildConfigField("String", "SCANNER_${key.uppercase()}", "\"${scannerDeployment.getProperty(key)}\"")
        }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
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
    testImplementation("org.json:json:20240303")
    androidTestImplementation("androidx.test:runner:1.5.2")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
}
