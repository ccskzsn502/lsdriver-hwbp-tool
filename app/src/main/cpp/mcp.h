#pragma once

// 启动 stdio MCP Server（JSON-RPC over stdin/stdout）
int run_mcp_server();

// 启动 HTTP MCP Server（REST API over HTTP），AI 可直接通过 HTTP 调用断点工具
int run_mcp_http_server(int port = 37662);