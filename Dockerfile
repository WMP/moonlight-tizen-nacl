FROM moonlight-tizen-nacl-sdk:5.6 AS build

USER moonlight
WORKDIR /home/moonlight

ENV PATH=/home/moonlight/tizen-studio/tools/ide/bin:/home/moonlight/tizen-studio/tools:${PATH}
ENV NACL_SDK_ROOT=/home/moonlight/pepper_63

# Copy project sources only after SDK/toolchain is already cached.
COPY --chown=moonlight . ./moonlight-tizen-nacl

RUN cd moonlight-tizen-nacl && \
    make

RUN mkdir -p build/static build/pnacl/Release

RUN pepper_63/toolchain/linux_pnacl/bin/pnacl-translate \
    -arch arm \
    moonlight-tizen-nacl/pnacl/Release/moonlight-chrome.pexe \
    -o build/pnacl/Release/moonlight-chrome-arm.nexe

RUN cp -r moonlight-tizen-nacl/index.html \
         moonlight-tizen-nacl/config.xml \
         moonlight-tizen-nacl/icons/icon128.png \
         moonlight-tizen-nacl/*.nmf \
         build/ && \
    cp -r moonlight-tizen-nacl/static/* build/static/ && \
    mv build/icon128.png build/icon.png && \
    mv build/*.nmf build/pnacl/Release/

RUN echo \
    'set timeout -1\n' \
    'spawn tizen package -t wgt -- build\n' \
    'expect "Author password:"\n' \
    'send -- "1234\\r"\n' \
    'expect "Yes: (Y), No: (N) ?"\n' \
    'send -- "N\\r"\n' \
    'expect eof\n' \
| expect

RUN mv build/MoonlightNaCl.wgt /home/moonlight/MoonlightNaCl.wgt

# Final image still has Tizen tools and sdb, so you can install from inside container.
FROM moonlight-tizen-nacl-sdk:5.6

USER moonlight
WORKDIR /home/moonlight

ENV PATH=/home/moonlight/tizen-studio/tools/ide/bin:/home/moonlight/tizen-studio/tools:${PATH}
ENV NACL_SDK_ROOT=/home/moonlight/pepper_63

COPY --from=build /home/moonlight/MoonlightNaCl.wgt /home/moonlight/MoonlightNaCl.wgt
