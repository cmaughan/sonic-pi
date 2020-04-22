%% Sonic Pi API server process
%% --
%% This file is part of Sonic Pi: http://sonic-pi.net
%% Full project source: https://github.com/samaaron/sonic-pi
%% License: https://github.com/samaaron/sonic-pi/blob/master/LICENSE.md
%%
%% Copyright 2016,2017 by Joe Armstrong (http://joearms.github.io/)
%% All rights reserved.
%%
%% Permission is granted for use, copying, modification, and
%% distribution of modified versions of this work as long as this
%% notice is included.
%% ++

-module(pi_server_api).

-export([start_link/1]).

%% internal
-export([init/2, loop/1]).

%% sys module callbacks
-export([system_continue/3, system_terminate/4, system_code_change/4,
         system_get_state/1, system_replace_state/2]).

-define(APPLICATION, pi_server).
-define(SERVER, ?MODULE).

%% time between idling messages
-define(IDLE_TIME, 60000).

%% Bundles whose delay time is not greater than NODELAY_LIMIT
%% are forwarded directly without starting a timer.
-define(NODELAY_LIMIT, 1).

-import(pi_server_util,
        [log/2, debug/2, debug/3]).


%% Bundle Commands
%% ===============

%%   ["/send_after", Host, Port | Cmd]
%%   ["/send_after_tagged", Tag, Host, Port | Cmd]
%%
%%   Both commands send the OSC message <Cmd> to <Host,Port>
%%   at the time in the bundle header
%%
%% Immediate Commands
%%  ["/flush", <Tag>]

%% Tagged send_after's
%%   A Tag can be associated with a send-after command
%%   If no tag is explicitly named the tag called "default" is assumed
%%   ["/flush", Tag] cancels all send-after commands which have not yet
%%   been issued.
%%
%% Examples:
%%   ["/flush", "default"]
%%      cancels all send-after requests that were scheduled with
%%      a ["/send_after", Host, Port, ...] bundle
%%   ["/flush", "drums"]
%%      cancels all send-after request that were scheduled with
%%      a ["/send_after_tagged,"drums", Host, Port, ...] bundle

%% Implementation notes:
%%  A hashmap (called TagMap) is added to the main loop of the server
%%   This is a map of the form #{Name1 => Pid1, Name2 => Pid2, ...}
%%   where the process PidN tracks the active timers for the tag NameN.
%%   New processes in the tagmap are created on demand.
%%   To flush a tag, we tell the corresponding tracker process to
%%   cancel its current timers.


%% supervisor compliant start function
start_link(CueServer) ->
    %% synchronous start of the child process
    proc_lib:start_link(?MODULE, init, [self(), CueServer]).


init(Parent, CueServer) ->
    register(?SERVER, self()),
    APIPort = application:get_env(?APPLICATION, api_port, undefined),
    io:format("~n"
              "+--------------------------------------+~n"
              "    This is the Sonic Pi API Server     ~n"
              "       Powered by Erlang ~s             ~n"
              "                                        ~n"
              "       API listening on port ~p         ~n"
              "+--------------------------------------+~n~n~n",
              [erlang:system_info(otp_release), APIPort]),

    {ok, APISocket} = gen_udp:open(APIPort, [binary, {ip, loopback}]),

    %% tell parent we have allocated resources and are up and running
    proc_lib:init_ack(Parent, {ok, self()}),

    debug(2, "listening for API commands on socket: ~p~n",
          [try erlang:port_info(APISocket) catch _:_ -> undefined end]),
    State = #{parent => Parent,
              api_socket => APISocket,
              cue_server => CueServer,
              tag_map => #{}
             },
    loop(State).

loop(State) ->
    receive
        {udp, _APISocket, _Ip, _Port, Bin} ->
            debug(3, "api server got UDP on ~p:~p~n", [_Ip, _Port]),
            case (catch osc:decode(Bin)) of
                {bundle, Time, X} ->
                    debug("got bundle for time ~f~n", [Time]),
                    NewState = do_bundle(Time, X, State),
                    ?MODULE:loop(NewState);
                {cmd, ["/flush", Tag]=Cmd} ->
                    debug_cmd(Cmd),
                    {Tracker, NewState} = tracker_pid(Tag, State),
                    pi_server_tracker:flush(all, Tracker),
                    ?MODULE:loop(NewState);
                {cmd, ["/internal-cue-port", Flag]=Cmd} ->
                    debug_cmd(Cmd),
                    send_to_cue({internal, Flag =:= 1}, State),
                    ?MODULE:loop(State);
                {cmd, ["/stop-start-cue-server", Flag]=Cmd} ->
                    debug_cmd(Cmd),
                    send_to_cue({enabled, Flag =:= 1}, State),
                    ?MODULE:loop(State);
                {cmd, Cmd} ->
                    log("Unknown command: \"~s\"~n", [Cmd]),
                    ?MODULE:loop(State);
                {'EXIT', Why} ->
                    log("Error decoding: ~p ~p~n",[Bin, Why]),
                    ?MODULE:loop(State)
            end;
        {system, From, Request} ->
            %% handling system messages (like a gen_server does)
            sys:handle_system_msg(Request, From,
                                  maps:get(parent, State),
                                  ?MODULE, [], State);
        Any ->
            log("API Server got unexpected message: ~p~n", [Any]),
            ?MODULE:loop(State)
    after ?IDLE_TIME ->
            debug(2, "api process idling~n", []),
            ?MODULE:loop(State)
    end.

send_to_cue(Message, State) ->
    CueServer = maps:get(cue_server, State),
    CueServer ! Message,
    ok.

debug_cmd([Cmd|Args]) ->
    debug("command: ~s ~p~n", [Cmd, Args]).

do_bundle(Time, [{_,B}], State) ->
    {cmd, Cmd} = osc:decode(B),
    case Cmd of
        ["/send_after", Host, Port | Cmd1] ->
            schedule_cmd("default", Time, Host, Port, Cmd1, State);
        ["/send_after_tagged", Tag, Host, Port | Cmd1] ->
            schedule_cmd(Tag, Time, Host, Port, Cmd1, State);
        _ ->
            log("Unexpected bundle:~p~n", [Cmd]),
            State
    end.

%% Schedules a command for forwarding (or forwards immediately)
schedule_cmd(Tag, Time, Host, Port, Cmd, State) ->
    {Tracker, NewState} = tracker_pid(Tag, State),
    Data = {Host, Port, osc:encode(Cmd)},
    Delay = Time - osc:now(),
    MsDelay = trunc(Delay*1000+0.5), %% nearest
    if MsDelay > ?NODELAY_LIMIT ->
            Msg = {forward, Time, Data, Tracker},
            %% Note: lookup of the registered server name will happen
            %% when the timer triggers, and if no such process exists
            %% at that time, the message will be quietly dropped
            CueServer = maps:get(cue_server, State),
            Timer = erlang:start_timer(MsDelay, CueServer, Msg),
            debug(2, "start timer of ~w ms for time ~f~n", [MsDelay, Time]),
            pi_server_tracker:track(Timer, Time, Tracker);
       true ->
            send_to_cue({forward, Time, Data}, NewState),
            debug(2, "directly forward message for delay ~f~n", [Delay])
    end,
    NewState.

%% Get the pid for the tag group tracker, creating it if needed
tracker_pid(Tag, State) ->
    TagMap = maps:get(tag_map, State),
    case maps:find(Tag, TagMap) of
        {ok, Pid} ->
            {Pid, State};
        error ->
            Pid = pi_server_tracker:start_link(Tag),
            debug("start new tracker process for tag \"~s\"~n", [Tag]),
            {Pid, State#{tag_map := maps:put(Tag, Pid, TagMap)}}
    end.


%% sys module callbacks

system_continue(_Parent, _Debug, State) ->
    loop(State).

system_terminate(Reason, _Parent, _Debug, _State) ->
    exit(Reason).

system_code_change(_State, _Module, _OldVsn, _Extra) ->
    ok.

system_get_state(InternalState) ->
    ExternalState = InternalState,
    {ok, ExternalState}.

system_replace_state(StateFun, InternalState) ->
    ExternalState = InternalState,
    NewExternalState = StateFun(ExternalState),
    NewInternalState = NewExternalState,
    {ok, NewExternalState, NewInternalState}.