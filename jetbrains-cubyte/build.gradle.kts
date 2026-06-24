// build.gradle.kts — JetBrains IntelliJ Platform plugin build configuration.
//
// Uses the official `org.jetbrains.intellij.platform` Gradle plugin, which
// sets up the IDE sandbox, resolves the platform dependency, and produces
// a distributable .zip of the plugin under `build/distributions/`.
//
// Required CLI command to build:  ./gradlew buildPlugin
// Required CLI command to test:   ./gradlew runIde
//
// TODO: Replace `cubyte-lsp` version in dependencies once the language
// server is wired up. The TextMate grammar handles highlighting only;
// the LSP provides diagnostics/completions/go-to-def.

plugins {
    id("java")
    id("org.jetbrains.intellij.platform") version "2.0.1"
    kotlin("jvm") version "1.9.24"
}

group = "com.cubyte"
version = "0.0.1"

repositories {
    mavenCentral()
    intellijPlatform {
        // IntelliJ Platform artifacts (the IDE distribution we build against).
        defaultRepositories()
    }
}

// Pulls in the IntelliJ IDEA Community distribution. We target a recent
// stable version that supports the TextMate bundle provider API.
// TODO: Bump to a current stable IDE version before release.
intellijPlatform {
    ideVersion.set("2024.2")
    pluginConfiguration {
        // Plugin ID and human-readable name live in gradle.properties.
        id = project.findProperty("pluginId") as String
        name = project.findProperty("pluginName") as String
        version = project.findProperty("pluginVersion") as String
    }
}

dependencies {
    // No third-party Kotlin libs yet — the TextMate JSON file does all
    // the syntax-highlighting work, so this plugin is intentionally
    // dependency-free on the Java/Kotlin side.
}

tasks {
    // Java/Kotlin target matching IntelliJ 2024.2 (Java 17).
    withType<JavaCompile> {
        sourceCompatibility = "17"
        targetCompatibility = "17"
    }
    withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
        kotlinOptions.jvmTarget = "17"
    }
}