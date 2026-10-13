# The x86_64 Android build, for the emulator: the Android image plus the x86_64
# Qt kit. Driven by android/build.sh --x86_64.
#
# A separate image rather than a layer in android.Dockerfile, so adding it
# rebuilds nothing below it and an arm64 build never carries the second kit.
FROM quack-build:android

# Kept in step with android.Dockerfile's, so both kits are the same Qt.
ARG QT_VERSION=6.11.1
ENV QUACK_QT_ANDROID_X86_64=/opt/Qt/${QT_VERSION}/android_x86_64
RUN /opt/aqt/bin/aqt install-qt linux android ${QT_VERSION} android_x86_64 \
        --outputdir /opt/Qt -m qtmultimedia \
    && rm -rf /root/.cache
