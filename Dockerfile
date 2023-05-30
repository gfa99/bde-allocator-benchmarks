# This Dockerfile provides a development environment for these benchmarks.
# It can also be reproduced on a local machine to run without virtualization.

from ubuntu:jammy

ENV HOME="/root"
WORKDIR $HOME

RUN apt update -y
RUN apt upgrade -y
RUN apt install -y build-essential git time

# The old build tool is not compatible with Python 3.7+ (PEP 479).
# https://github.com/pyenv/pyenv/wiki#suggested-build-environment
RUN DEBIAN_FRONTEND=noninteractive apt install -y \
    build-essential libssl-dev zlib1g-dev libbz2-dev libreadline-dev libsqlite3-dev curl \
    libncursesw5-dev xz-utils tk-dev libxml2-dev libxmlsec1-dev libffi-dev liblzma-dev
RUN curl https://pyenv.run | bash
ENV PYENV_ROOT="$HOME/.pyenv"
ENV PATH="$PYENV_ROOT/bin:$PATH"
RUN eval "$(pyenv init -)"
RUN pyenv install 3.6
RUN pyenv global 3.6

# This seems to be the latest tagged version still based on WAF.
RUN git clone https://github.com/bloomberg/bde-tools.git --branch v1.1 --depth 1
ENV PATH="$HOME/bde-tools/bin:$PATH"

RUN echo "export PYENV_ROOT=\"$HOME/.pyenv\"" >> $HOME/.bashrc
RUN echo "export PATH=\"$PYENV_ROOT/bin:\$PATH\"" >> $HOME/.bashrc
RUN echo 'eval "$(pyenv init -)"' >> $HOME/.bashrc
RUN echo "export PATH=\"$HOME/bde-tools/bin:\$PATH\"" >> $HOME/.bashrc

COPY . $HOME/bde

CMD bash
