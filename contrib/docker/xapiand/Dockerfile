################################################################################
# __  __           _                 _
# \ \/ /__ _ _ __ (_) __ _ _ __   __| |
#  \  // _` | '_ \| |/ _` | '_ \ / _` |
#  /  \ (_| | |_) | | (_| | | | | (_| |
# /_/\_\__,_| .__/|_|\__,_|_| |_|\__,_|
#           |_|
#
# Build using:
# docker build -t dubalu/xapiand:latest contrib/docker/xapiand
# docker tag dubalu/xapiand:latest dubalu/xapiand:$(docker run --rm dubalu/xapiand:latest --version)
# docker push dubalu/xapiand

FROM dubalu/xapian as builder

# Bust cache
ADD https://api.github.com/repos/Kronuz/Xapiand/git/refs/heads/master version.json

# Build Xapiand
RUN CONFIG="\
    -DCMAKE_INSTALL_PREFIX:PATH=/usr \
  " \
  && apk add libuuid \
  && apk add --no-cache --virtual .build-deps \
    ninja \
    cmake \
    git \
    zlib-dev \
    util-linux-dev \
  && mkdir -p /usr/src \
  && git clone --single-branch --depth=1 "https://github.com/Kronuz/Xapiand" /usr/src/Xapiand \
  && mkdir /usr/src/Xapiand/build \
  && cd /usr/src/Xapiand/build \
  && cmake -GNinja $CONFIG .. \
  && ninja install \
  && rm -rf /usr/src/Xapiand \
  && apk del .build-deps \
  && apk del build-clang

# Xapiand image
FROM alpine:3.8

MAINTAINER Kronuz

COPY --from=builder /usr /usr
COPY ./entrypoint.sh /

RUN apk add libuuid \
  && mkdir -p /var/db \
  && addgroup -S xapiand \
  && adduser -D -S -h /var/db/xapiand -s /sbin/nologin -G xapiand xapiand \
  && chmod +x /entrypoint.sh

EXPOSE 8880

ENTRYPOINT ["/entrypoint.sh"]
CMD ["-v"]