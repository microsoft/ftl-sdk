FROM debian:9

RUN apt-get update -y
RUN apt-get install wget -y
RUN apt-get install -y cmake
RUN apt-get install git -y
RUN apt-get install build-essential -y
RUN apt-get install libssl-dev -y
RUN apt-get install libcurl4-openssl-dev -y
RUN apt-get upgrade ca-certificates -y

RUN apt-get install -y jq curl

RUN useradd ftl-user

RUN mkdir -p /opt/ftl-sdk/vid

RUN chown -R ftl-user:ftl-user /opt/ftl-sdk

WORKDIR /opt/ftl-sdk/vid

ARG VIDEO_URL=https://videotestmedia.blob.core.windows.net/ftl/sintel.h264
RUN wget ${VIDEO_URL}
ARG AUDIO_URL=https://videotestmedia.blob.core.windows.net/ftl/sintel.opus
RUN wget ${AUDIO_URL}

COPY --chown=ftl-user:ftl-user ./CMakeLists.txt /opt/ftl-sdk/CMakeLists.txt
COPY --chown=ftl-user:ftl-user ./libcurl /opt/ftl-sdk/libcurl
COPY --chown=ftl-user:ftl-user ./libjansson /opt/ftl-sdk/libjansson
COPY --chown=ftl-user:ftl-user ./libftl /opt/ftl-sdk/libftl
COPY --chown=ftl-user:ftl-user ./Doxyfile /opt/ftl-sdk/Doxyfile
COPY --chown=ftl-user:ftl-user ./ftl_app /opt/ftl-sdk/ftl_app
COPY --chown=ftl-user:ftl-user ./get-video /opt/ftl-sdk/get-video
COPY --chown=ftl-user:ftl-user ./get-audio /opt/ftl-sdk/get-audio
COPY --chown=ftl-user:ftl-user ./scripts /opt/ftl-sdk/scripts

USER ftl-user

WORKDIR /opt/ftl-sdk

RUN ./scripts/build

COPY --chown=ftl-user:ftl-user ./start-stream /opt/ftl-sdk/start-stream

ENTRYPOINT ["./start-stream"]

