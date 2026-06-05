#!/usr/bin/env lua
-- SPDX-License-Identifier: BSD-3-Clause
-- Replay CMSG IPC traces produced by cmsg_trace_message().

local function die(msg, code)
    io.stderr:write("error: " .. msg .. "\n")
    os.exit(code or 2)
end

local function trim(s)
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function shell_quote(s)
    return "'" .. tostring(s):gsub("'", [['"'"']]) .. "'"
end

local function parse_iso_timestamp(ts)
    local year, mon, day, hour, min, sec, millis = 
        ts:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d):(%d%d)%.(%d+)$")
    if not year then
        return nil, "Invalid timestamp format '" .. ts .. "'"
    end

    local epoch = os.time({
        year = tonumber(year),
        month = tonumber(mon),
        day = tonumber(day),
        hour = tonumber(hour),
        min = tonumber(min),
        sec = tonumber(sec),
    })
    if not epoch then
        return nil, "Unable to convert timestamp '" .. ts .. "'"
    end

    local frac = tonumber("0." .. millis) or 0
    return epoch + frac
end

local function sleep_seconds(seconds)
    if seconds <= 0 then
        return
    end

    local ok_socket, socket = pcall(require, "socket")
    if ok_socket and socket and socket.sleep then
        socket.sleep(seconds)
        return
    end

    os.execute("sleep " .. string.format("%.3f", seconds))
end

local function parse_entries(path)
    local f, err = io.open(path, "r")
    if not f then
        die(err)
    end

    local entries = {}
    local function derive_service_from_trace_service(service_root)
        local package_name, service_name = service_root:match("^([^.]+)%.(.+)$")
        if not package_name or not service_name then
            die("Cannot derive package/service from trace service '" .. service_root .. "'. Expected '<package>.<service>'")
        end
        return {
            file_name = service_root:gsub("%.", "_"),
            package_name = package_name,
            service_name = service_name:gsub("%.", "_"),
        }
    end

    local line_no = 0
    for line in f:lines() do
        local stripped = trim(line)
        line_no = line_no + 1

        if stripped ~= "" then
            local timestamp = stripped:match('"timestamp":"([^"]*)"')
            local service = stripped:match('"service":"([^"]*)"')
            local method = stripped:match('"method":"([^"]*)"')
            local msg_pos = stripped:find('"msg":')
            local msg = msg_pos and stripped:sub(msg_pos + 6, -2) or nil

            if not timestamp or not service or not method or not msg then
                f:close()
                die("Invalid trace line " .. line_no)
            end

            entries[#entries + 1] = {
                timestamp = timestamp,
                method = method,
                trace_service = derive_service_from_trace_service(service),
                payload = msg,
            }
        end
    end

    f:close()
    return entries
end

local function parse_simple_service_map(path)
    if not path then
        return {}
    end

    local f, err = io.open(path, "r")
    if not f then
        die(err)
    end

    local map = {}
    local lineno = 0

    for line in f:lines() do
        lineno = lineno + 1
        local stripped = trim(line)
        if stripped ~= "" and stripped:sub(1, 1) ~= "#" then
            local root, file_name, package_name, service_name = 
                stripped:match("^(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s*$")
            if not root then
                die("Invalid service-map line " .. lineno .. ". Expected: <root> <file_name> <package_name> <service_name>")
            end

            map[root] = {
                file_name = file_name,
                package_name = package_name,
                service_name = service_name,
            }
        end
    end

    f:close()
    return map
end

local function derive_service_from_root(root, file_template)
    local package_name, service_name = root:match("^([^.]+)%.(.+)$")
    if not package_name then
        die("Cannot derive package/service from '" .. root .. "'. Expected '<package>.<service>'")
    end

    local file_name
    if file_template and file_template ~= "" then
        file_name = file_template
        file_name = file_name:gsub("{package}", package_name)
        file_name = file_name:gsub("{service}", service_name)
        file_name = file_name:gsub("{root}", root)
    else
        file_name = service_name
    end

    return {
        file_name = file_name,
        package_name = package_name,
        service_name = service_name,
    }
end

local function resolve_service(entry, opts, service_map)
    if entry.trace_service then
        return entry.trace_service
    end

    if service_map[entry.root] then
        return service_map[entry.root]
    end

    if opts.file_name and opts.package_name and opts.service_name then
        return {
            file_name = opts.file_name,
            package_name = opts.package_name,
            service_name = opts.service_name,
        }
    end

    if opts.derive_from_root then
        return derive_service_from_root(entry.root, opts.file_template)
    end

    die("No service definition for entry root '" .. entry.root .. "'. Use --service-map, static --file-name/--package-name/--service-name, or --derive-from-root.")
end

local function build_cmsg_command(entry, service, opts)
    local parts = {
        shell_quote(opts.cmsg_bin),
        "-t",
        shell_quote(opts.transport),
        "-f",
        shell_quote(service.file_name),
        "-p",
        shell_quote(service.package_name),
        "-s",
        shell_quote(service.service_name),
        "-a",
        shell_quote(entry.method),
        "-m",
        shell_quote(entry.payload),
    }

    if opts.one_way then
        parts[#parts + 1] = "-o"
    end

    if opts.disable_error_logs then
        parts[#parts + 1] = "-q"
    end

    if opts.timeout then
        parts[#parts + 1] = "-T"
        parts[#parts + 1] = shell_quote(tostring(opts.timeout))
    end

    if opts.transport == "tcp" then
        if not opts.port_service_name or not opts.ip_address then
            die("--port-service-name and --ip-address are required for TCP")
        end
        parts[#parts + 1] = "-n"
        parts[#parts + 1] = shell_quote(opts.port_service_name)
        parts[#parts + 1] = "-r"
        parts[#parts + 1] = shell_quote(opts.ip_address)
    end

    return table.concat(parts, " ")
end

local function run_command_capture(cmd)
    local pipe = io.popen(cmd .. " 2>&1")
    if not pipe then
        return false, "failed to execute command"
    end

    local output = pipe:read("*a")
    local ok, why, code = pipe:close()

    if ok == nil then
        return false, output, why, code
    end

    return true, output, why, code
end

local function print_usage()
    io.stdout:write([[
Usage: cmsg_trace_replay.lua --input FILE [options]

Required:
  --input FILE                    Trace log file path

General:
  --cmsg-bin PATH                 Path to cmsg binary (default: cmsg)
  --transport unix|tcp            Replay transport (default: unix)
  --timeout SECONDS               Receive timeout
  --one-way                       Use one-way cmsg calls
  --disable-error-logs            Pass -q to cmsg

TCP:
  --port-service-name NAME        Service name from /etc/services
  --ip-address IPV4               Server IPv4 address

Service resolution:
    (default)                      Read service=<package>.<service> from each trace entry
  --file-name NAME                Static proto file base name
  --package-name NAME             Static package name
  --service-name NAME             Static service name
  --service-map FILE              Map file: <root> <file> <package> <service>
  --derive-from-root              Derive from <package>.<service> root
  --file-template TEMPLATE        Uses {package}, {service}, {root}

Replay control:
  --realtime                      Preserve recorded timing deltas
  --speed MULTIPLIER              Replay speed when --realtime (default: 1.0)
  --max-sleep SECONDS             Max per-entry delay when --realtime
  --dry-run                       Print commands only
  --continue-on-error             Continue after invocation failures
]])
end

local function parse_args(argv)
    local opts = {
        cmsg_bin = "cmsg",
        transport = "unix",
        speed = 1.0,
        one_way = false,
        disable_error_logs = false,
        derive_from_root = false,
        realtime = false,
        dry_run = false,
        continue_on_error = false,
    }

    local i = 1
    while i <= #argv do
        local a = argv[i]

        if a == "--help" or a == "-h" then
            print_usage()
            os.exit(0)
        elseif a == "--one-way" then
            opts.one_way = true
        elseif a == "--disable-error-logs" then
            opts.disable_error_logs = true
        elseif a == "--derive-from-root" then
            opts.derive_from_root = true
        elseif a == "--realtime" then
            opts.realtime = true
        elseif a == "--dry-run" then
            opts.dry_run = true
        elseif a == "--continue-on-error" then
            opts.continue_on_error = true
        elseif a:sub(1, 2) == "--" then
            local key = a:sub(3)
            i = i + 1
            if i > #argv then
                die("Missing value for --" .. key)
            end
            local value = argv[i]

            if key == "input" then
                opts.input = value
            elseif key == "cmsg-bin" then
                opts.cmsg_bin = value
            elseif key == "transport" then
                opts.transport = value
            elseif key == "port-service-name" then
                opts.port_service_name = value
            elseif key == "ip-address" then
                opts.ip_address = value
            elseif key == "timeout" then
                opts.timeout = tonumber(value)
                if not opts.timeout then
                    die("Invalid numeric value for --timeout")
                end
            elseif key == "file-name" then
                opts.file_name = value
            elseif key == "package-name" then
                opts.package_name = value
            elseif key == "service-name" then
                opts.service_name = value
            elseif key == "service-map" then
                opts.service_map = value
            elseif key == "file-template" then
                opts.file_template = value
            elseif key == "speed" then
                opts.speed = tonumber(value)
                if not opts.speed then
                    die("Invalid numeric value for --speed")
                end
            elseif key == "max-sleep" then
                opts.max_sleep = tonumber(value)
                if not opts.max_sleep then
                    die("Invalid numeric value for --max-sleep")
                end
            else
                die("Unknown option --" .. key)
            end
        else
            die("Unknown argument: " .. a)
        end

        i = i + 1
    end

    if not opts.input then
        die("--input is required")
    end

    if opts.transport ~= "unix" and opts.transport ~= "tcp" then
        die("--transport must be 'unix' or 'tcp'")
    end

    if opts.speed <= 0 then
        die("--speed must be greater than 0")
    end

    if opts.file_template and not opts.derive_from_root then
        die("--file-template requires --derive-from-root")
    end

    local static_count = 0
    if opts.file_name then
        static_count = static_count + 1
    end
    if opts.package_name then
        static_count = static_count + 1
    end
    if opts.service_name then
        static_count = static_count + 1
    end

    if static_count ~= 0 and static_count ~= 3 then
        die("Static service configuration requires --file-name, --package-name and --service-name")
    end

    return opts
end

local function replay(entries, opts, service_map)
    local failures = 0
    local prev_ts = nil

    for idx, entry in ipairs(entries) do
        local service = resolve_service(entry, opts, service_map)

        if opts.realtime then
            local cur_ts, err = parse_iso_timestamp(entry.timestamp)
            if not cur_ts then
                die(err)
            end

            if prev_ts then
                local delta = cur_ts - prev_ts
                if delta > 0 then
                    local scaled = delta / opts.speed
                    if opts.max_sleep and scaled > opts.max_sleep then
                        scaled = opts.max_sleep
                    end
                    sleep_seconds(scaled)
                end
            end

            prev_ts = cur_ts
        end

        io.stdout:write(string.format("[%d/%d] %s %s.%s.%s\n", idx, #entries, entry.timestamp, service.package_name, service.service_name, entry.method))

        local cmd = build_cmsg_command(entry, service, opts)
        if opts.dry_run then
            io.stdout:write("  DRY-RUN: " .. cmd .. "\n")
        else
            local ok, output = run_command_capture(cmd)
            if output and output ~= "" then
                io.stdout:write(output)
            end
            if not ok then
                failures = failures + 1
                io.stderr:write("Replay failed for method " .. entry.method .. "\n")
                if not opts.continue_on_error then
                    break
                end
            end
        end
    end

    return failures
end

local function main()
    local opts = parse_args(arg)
    local entries = parse_entries(opts.input)
    if #entries == 0 then
        die("No trace entries found in input")
    end

    local service_map = parse_simple_service_map(opts.service_map)
    local failures = replay(entries, opts, service_map)

    if failures > 0 then
        os.exit(1)
    end
    os.exit(0)
end

main()
