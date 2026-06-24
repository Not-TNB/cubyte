// settings.gradle.kts — Gradle settings.
//
// Sets the root project name and enables the IntelliJ Platform plugin's
// version catalog. Nothing else needs to change unless additional
// subprojects are introduced later.

rootProject.name = "jetbrains-cubyte"

pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)
    repositories {
        mavenCentral()
        intellijPlatform {
            defaultRepositories()
        }
    }
}