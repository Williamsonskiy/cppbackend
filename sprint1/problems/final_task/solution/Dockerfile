FROM gcc:12.5 as build

RUN apt update && \
    apt install -y \
      python3-pip \
      cmake \
      python3-venv

# Создать виртуальную среду для Python
RUN python3 -m venv /opt/venv

# Принудительно указать использование виртуальной среды в переменной среды PATH
ENV PATH="/opt/venv/bin:$PATH"

RUN pip install --no-cache-dir conan==1.*

# Запуск conan
COPY conanfile.txt /app/
RUN mkdir /app/build && cd /app/build && \
    conan install .. --build=missing

COPY ./src /app/src
COPY CMakeLists.txt /app/

RUN cd /app/build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    cmake --build .

# Второй контейнер
FROM ubuntu:24.04 as run

# Создадим пользователя www
RUN groupadd -r www && useradd -r -g www www
USER www

# Скопируем приложение и дату (добавлена подпапка bin/)
COPY --from=build /app/build/bin/hello_async /app/
COPY ./data /app/data

# Запускаем игровой сервер
ENTRYPOINT ["/app/hello_async", "/app/data/config.json"]
