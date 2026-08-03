FROM node:22-alpine AS frontend

WORKDIR /build
COPY package.json package-lock.json ./
RUN npm ci
COPY index.html tsconfig.json vite.config.ts ./
COPY public ./public
COPY src ./src
RUN npm run build

FROM python:3.13-slim AS runtime

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    TABUI_HOST=0.0.0.0 \
    TABUI_PORT=8080 \
    TABUI_DATA_DIR=/data

WORKDIR /app
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt
COPY backend ./backend
COPY server.py ./
COPY --from=frontend /build/dist ./dist

RUN useradd --system --uid 10001 --create-home tabui && mkdir -p /data && chown -R tabui:tabui /app /data
USER tabui

EXPOSE 8080
VOLUME ["/data"]
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
  CMD python -c "import json,urllib.request; assert json.load(urllib.request.urlopen('http://127.0.0.1:8080/health', timeout=2))['ok']"

CMD ["python", "server.py"]
