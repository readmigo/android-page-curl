plugins {
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.android) apply false
}

// Group used for dependency substitution in composite builds
group = "io.github.readmigo"
version = "1.0.0"
