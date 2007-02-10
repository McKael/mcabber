#!/usr/bin/ruby -w
#
#  Copyright (C) 2006,2007 Adam Wolk "Mulander" <netprobe@gmail.com>
#  Copyright (C) 2006 Mateusz Karkula "Karql"
#
# This script is provided under the terms of the GNU General Public License,
# see the file COPYING in the root mcabber source directory.
#
#

require 'getoptlong'

##
# strings of colors ;)
module Colors
  @@color = true

  ESC 	= 27.chr

  RED 	= ESC + '[31m'
  GREEN = ESC + '[32m'
  YELLOW= ESC + '[33m'
  BLUE  = ESC + '[34m'
  PURPLE= ESC + '[35m'
  CYAN  = ESC + '[36m'

  BGREEN= ESC + '[42m'

  ENDCOL= ESC + '[0m'

  def color(color)
    return '[' + self + ']' unless @@color
    color + self + ENDCOL
  end
  def red;	color(RED);	end
  def green;	color(GREEN);	end
  def yellow;	color(YELLOW);	end
  def blue;	color(BLUE);	end
  def purple;	color(PURPLE);	end
  def cyan;	color(CYAN);	end
  def bgreen;	color(BGREEN);	end
end

class String;  include Colors;	end

class Option
  attr_accessor :value, :current, :default
  attr_reader :name,:msg

  def initialize(args)
    @name	= args[:name]
    @msg	= args[:msg]
    @default	= args[:default]
    @value	= nil
    @current	= nil
    @prompt	= ''
  end

  def set?;		!@value.nil?;		end
  def to_s;		"set #@name=#@value";	end
  def additional?; 	false;			end

  def ask()
    puts @msg
    print @prompt
    $stdin.gets.chomp
  end

end

class YesNo < Option
  def initialize(args)
    super(args)
    @ifSet  = args[:ifSet]
    @prompt = '[Yes/no]: '
  end
  def ask()
    # 1 == yes, 0 == no
    case super
      when /^Y/i
        @value = 1
	return additional()
      when /^N/i
        @value = 0
	return []
      else
        puts 'Please answer yes or no'
	puts
        ask()
      end
  end

  def additional?
    (@value == 1 && !@ifSet.nil?) ? true : false
  end

  def additional
    (@ifSet.nil?) ? [] : @ifSet
  end
end

class Edit < Option
  def initialize(args)
    super(args)
    @regex  = args[:regex]
    @prompt = '[edit]: '
  end
  def ask()
    answer = super
    if answer.empty? || ( !@regex.nil? && !(answer =~ @regex) )
      ask()
    else
      @value = answer
    end
    return []
  end
end

class Multi < Option
  attr_reader :choices
  def initialize(args)
    super(args)
    @choices = args[:choices]
    @max     = @choices.length - 1
    @prompt  = "[0-#{ @max }] "
  end

  def ask()
    puts  @msg
    @choices.each_with_index do |choice,idx|
      print "#{idx}. #{choice}\n"
    end
    print @prompt
    answer = $stdin.gets.chomp

    ask() if answer.empty? # we ask here because ''.to_i == 0

    case answer.to_i
      when 0 ... @max
        @value = answer
      else
        ask()
    end
    return []
  end
end

class Wizzard
  VERSION = 0.04
  attr_accessor :options, :ignore_previous, :ignore_auto, :target
  def initialize()
    if File.exists?(ENV['HOME'] + '/.mcabberrc')
      @target = ENV['HOME'] + '/.mcabberrc'
    else
      Dir.mkdir(ENV['HOME'] + '/.mcabber') unless File.exists?(ENV['HOME'] + '/.mcabber')
      @target = ENV['HOME'] + '/.mcabber/mcabberrc'
    end
    @ignore_previous = false
    @ignore_auto     = false
    @options   = Hash.new
    @order     = Array.new
    @processed = Array.new
    @old     = Array.new # for storing the users file untouched
  end

  ##
  # add a group of settings to the order queue
  def enqueue(group)
    group = group.to_a if group.class.to_s == 'String'
    @order += group
  end


  ## adds options to the settings object
  def add(args)
    @options[args[:name]] = args[:type].new(args)
  end

  ## run the wizzard
  def run()
    parse()
    display(@order)
    save()
  end

  ##
  # displays the setting and allows the user to modify it
  def display(order)
    order.each do |name|
      # this line here is less efficient then on the end of this method
      # but if placed on the end, recursion then breaks the order of settings
      @processed.push(name)
      ##
      # I know this is not efficient, but I have no better idea to modify the default port
      @options['port'].default = 5223 if @options['ssl'].value == 1

      puts
      puts "'#{name}'"
      puts @options[name].msg
      puts 'e'.green + 'dit setting'
      puts 'l'.green + 'eave current setting ' + show(@options[name],:current).cyan unless @options[name].current.nil?
      puts 'u'.green + 'se default ' + show(@options[name],:default).cyan unless @options[name].default.nil?
      puts 's'.green + 'kip'
      puts 'a'.red + 'bort configuration'
      print '[action]: '
      case $stdin.gets.chomp
        when /^s/
          next
        when /^l/
          @options[name].value = @options[name].current
	  display(@options[name].additional) if @options[name].additional?
        when /^u/
          @options[name].value = @options[name].default
	  display(@options[name].additional) if @options[name].additional?
        when /^e/
          additional = @options[name].ask
	  display(additional) if additional.empty?
	when /^a/
	  puts 'aborted!!'.red
	  exit
      end
     end
  end

  ##
  # this allows us to print 'yes' 'no' or descriptions of multi option settings
  # insted of just showing an integer
  def show(option,type)
    value = ''
    if type == :default
      value = option.default
    else
      value = option.current
    end

    case option.class.to_s
      when 'YesNo'
        return (value.to_i==1) ? 'yes' : 'no'
      when 'Multi'
        return option.choices[value.to_i]
      else
        return value.to_s
    end
  end

  ## save
  # save all settings to a file
  def save()
    flag,dumped = true,false
    target	= File.new(@target,"w")

    @old.each do |line|
      flag = false if line =~ /^#BEGIN AUTO GENERATED SECTION/
      flag = true  if line =~ /^#END AUTO GENERATED SECTION/
      if flag
        target << line
      elsif( !flag && !dumped )
        target << "#BEGIN AUTO GENERATED SECTION\n\n"
        @processed.each do |name|
          target << @options[name].to_s + "\n" if @options[name].set?
        end
	puts
	dumped = true
      end
    end

    unless dumped
      target << "#BEGIN AUTO GENERATED SECTION\n\n"
      @processed.each do |name|
        target << @options[name].to_s + "\n" if @options[name].set?
      end
      target << "#END AUTO GENERATED SECTION\n\n"
    end

    target.close
  end
  ## parse
  # attempt to load settings from file
  def parse()
    return if @ignore_previous
    return unless File.exists?(@target)
    keyreg = @options.keys.join('|')
    parse  = true
    File.open(@target) do |config|
      config.each do |line|

        @old << line
	parse = false if @ignore_auto && line =~ /^#BEGIN AUTO GENERATED SECTION/
	parse = true  if @ignore_auto && line =~ /^#END AUTO GENERATED SECTION/

	if parse && line =~ /^set\s+(#{keyreg})\s*=\s*(.+)$/
	  @options[$1].current = $2 if @options.has_key?($1)
	end

      end
    end
  end

  ##
  # display onscreen help
  def Wizzard.help()
    puts %{
Usage: #{ $0.to_s.blue } #{ 'options'.green }

This script generates configuration files for mcabber jabber client

#{ "Options:".green }
#{ "-h".green }, #{ "--help".green }		display this help screen
#{ "-v".green }, #{ "--version".green }		display version information
#{ "-T".green }, #{ "--target".green }		configuration file
#{ "-i".green }, #{ "--ignore".green }		ignore previous configuration
#{ "-I".green }, #{ "--ignore-auto".green }	ignore auto generated section
#{ "-S".green }, #{ "--status".green }		ask for status settings
#{ "-P".green }, #{ "--proxy".green }		ask for proxy settings
#{ "-k".green }, #{ "--keep".green }		ping/keepalive connection settings
#{ "-t".green }, #{ "--tracelog".green }		ask for tracelog settings
#{ "-C".green }, #{ "--nocolor".green }		turn of color output
    }
    exit
  end

  ##
  # display version information
  def Wizzard.version()
    puts "mcwizz v#{VERSION.to_s.purple} coded by #{ 'Karql'.purple } & #{ 'mulander'.purple } <netprobe@gmail.com>"
    exit
  end
end

required	= %w{ 	username server resource nickname ssl port pgp logging }
proxy 		= %w{ 	proxy_host proxy_port proxy_user proxy_pass }
status		= %w{ 	buddy_format roster_width show_status_in_buffer autoaway message message_avail message_free
			message_dnd message_notavail message_away message_autoaway }
tracelog	= %w{	tracelog_level tracelog_file }

opts = GetoptLong.new(
  ["--help","-h",       GetoptLong::NO_ARGUMENT],
  ["--version","-v",    GetoptLong::NO_ARGUMENT],
  ["--target", "-T",	GetoptLong::REQUIRED_ARGUMENT],
  ["--ignore","-i",     GetoptLong::NO_ARGUMENT],
  ["--ignore-auto","-I",GetoptLong::NO_ARGUMENT],
  ["--proxy","-P",      GetoptLong::NO_ARGUMENT],
  ["--keep","-k",       GetoptLong::NO_ARGUMENT],
  ["--status","-S",     GetoptLong::NO_ARGUMENT],
  ["--tracelog","-t",   GetoptLong::NO_ARGUMENT],
  ["--nocolor","-C",   GetoptLong::NO_ARGUMENT]
)

opts.ordering = GetoptLong::REQUIRE_ORDER

config = Wizzard.new()
config.enqueue(required)
config.enqueue( %w{ beep_on_message hide_offline_buddies iq_version_hide_os autoaway } )

##
# Description of the add() syntax
# :name - name of the setting
# :msg  - message displayed to the user
# :type - type of settings - avaible types are: YesNo, Edit, Multi
# :default - default setting
# YesNo type specific flag:
# :ifSet  - an array of other options, that will be asked if the flag holding option is set to true
# Edit type specific flag:
# :regex- regular expression to which input will be compared
# Multi type specific flag:
# :choices - an array of possible settings
#

##
# here we add all the settings that we want to be able to handle

##
# ungrouped settings
config.add(	:name  => 'beep_on_message',
		:msg   => 'Should mcabber beep when you receive a message?',
		:type  => YesNo,
		:default => 0 )

config.add(	:name  => 'hide_offline_buddies',
		:msg   => 'Display only connected buddies in the roster?',
		:type  => YesNo,
		:default => 0 )

config.add(	:name  => 'pinginterval',
		:msg   => 'Enter pinginterval in seconds for keepalive settings' \
			  ' set this to 0 to disable.',
		:type  => Edit,
		:regex => /^\d+$/,
		:default => 40)

config.add(	:name  => 'iq_version_hide_os',
		:msg   => 'Hide Your OS information?',
		:type  => YesNo,
		:default => 0 )


config.add(	:name  => 'port',
		:msg   => 'Enter port number',
		:type  => Edit,
		:regex => /^\d+$/,
		:default => 5222 )

##
# server settings
config.add(	:name  => 'username',
		:msg   => 'Your username',
		:type  => Edit,
		:regex => /^[^\s\@:<>&\'"]+$/ )

config.add(	:name  => 'server',
		:msg   => 'Your jabber server',
		:type  => Edit,
		:regex => /^\S+$/ )

config.add(	:name  => 'resource',
		:msg   => 'Resource (If you don\'t know what a resource is, use the default setting)',
		:type  => Edit,
		:regex => /^.{1,1024}$/,
		:default => 'mcabber' )

config.add(	:name  => 'nickname',
		:msg   => 'Conference nickname (if you skip this setting your username will be used as' \
			  ' nickname in MUC chatrooms)',
		:type  => Edit )

##
# ssl settings
config.add(	:name  => 'ssl',
		:msg   => 'Enable ssl?',
		:type  => YesNo,
		:ifSet => %w{ ssl_verify ssl_cafile ssl_capath ciphers },
		:default => 0 )


config.add(	:name  => 'ssl_verify',
		:msg   => 'Set to 0 to disable certificate verification, or non-zero to set desired maximum CA' \
			  ' verification depth. Use -1 to specify an unlimited depth.',
		:type  => Edit,
		:regex => /^(-1)|(\d+)$/,
		:default => -1 )

config.add(	:name  => 'ssl_cafile',
		:msg   => 'Set to a path to a CA certificate file (may contain multiple CA certificates)',
		:type  => Edit )

config.add(	:name  => 'ssl_capath',
		:msg   => 'Set to a directory containing CA certificates (use c_rehash to generate hash links)',
		:type  => Edit )

config.add(	:name  => 'ciphers',
		:msg   => 'Set to a list of desired SSL ciphers (run "openssl ciphers" for a candidate values)',
		:type  => Edit )

##
# pgp support
config.add(	:name  => 'pgp',
		:msg   => 'Enable OpenPGP support?',
		:type  => YesNo,
		:ifSet => %w{ pgp_private_key },
		:default => 0 )

config.add(	:name  => 'pgp_private_key',
		:msg   => 'Enter your private key id. You can get the Key Id with gpg: ' \
			  '"gpg --list-keys --keyid-format long"',
		:type  => Edit )
##
# proxy settings
config.add(	:name  => 'proxy_host',
		:msg   => 'Proxy host',
		:type  => Edit,
		:regex => /^\S+?\.\S+?$/ )

config.add(	:name  => 'proxy_port',
		:msg   => 'Proxy port',
		:type  => Edit,
		:regex => /^\d+$/,
		:default => 3128 )

config.add(	:name  => 'proxy_user',
		:msg   => 'Proxy user',
		:type  => Edit )

config.add(     :name  => 'proxy_pass',
		:msg   => 'Proxy pass (will be stored unencrypted an the pass will be echoed during input)',
		:type  => Edit )
##
# trace logs
config.add(	:name  => 'tracelog_level',
		:msg   => 'Specify level of advanced traces',
		:type  => Multi,
		:choices => [ 	'lvl0: I don\'t want advanced tracing',
				'lvl1: most events of the log window are written to the file',
				'lvl2: debug logging (XML etc.)' ],
		:default => 0 )

config.add(	:name  => 'tracelog_file',
		:msg   => 'Specify a file to which the logs will be written',
		:type  => Edit )
##
# logging settings
config.add(	:name  => 'logging',
		:msg   => 'Enable logging?',
		:type  => YesNo,
		:ifSet => %w{ log_win_height log_display_sender load_logs logging_dir log_muc_conf},
		:default => 1 )

config.add(	:name  => 'log_win_height',
		:msg   => 'Set log window height (minimum 1)',
		:type  => Edit,
		:regex => /^[1-9]\d*/,
		:default => 5 )

config.add(	:name  => 'log_display_sender',
		:msg   => 'Display the message sender\'s jid in the log window?',
		:type  => YesNo,
		:default => 0 )

config.add(	:name  => 'load_logs',
		:msg   => 'Enable loading logs?',
		:type  => YesNo,
		:default => 1 )

config.add(	:name  => 'logging_dir',
		:msg   => 'Enter logging directory',
		:type  => Edit )

config.add(	:name  => 'log_muc_conf',
		:msg   => 'Log MUC chats?',
		:ifSet => %w{ load_muc_logs },
		:type  => YesNo,
		:default => 1 )

config.add(	:name  => 'load_muc_logs',
		:msg   => 'Load MUC chat logs?',
		:type  => YesNo,
		:default => 0 )
##
# status settings
config.add(	:name  => 'roster_width',
		:msg   => 'Set buddylist window width (minimum 2)',
		:type  => Edit,
		:regex => /^[2-9]\d*$/,
		:default => 24 )

config.add(	:name  => 'buddy_format',
		:msg   => 'What buddy format (in status window) do you prefer?',
		:type  => Multi,
		:choices => [ 	'<jid/resource>',
				'name <jid/resource> (name is omitted if same as the jid)',
				'name/resource (if the name is same as the jid, use <jid/res>',
				'name (if the name is the same as the jid, use <jid/res>' ] )

config.add(	:name  => 'show_status_in_buffer',
		:msg   => 'What status changes should be displayed in the buffer?',
		:type  => Multi,
		:choices => [	'none',
				'connect/disconnect',
				'all' ],
		:default => 2 )

config.add(	:name  => 'autoaway',
		:msg   => 'After how many seconds of inactivity should You become away? (0 for never)',
		:type  => Edit,
		:regex => /^\d+$/,
		:default => 0 )

config.add(	:name  => 'message',
		:msg   => 'Skip this setting unless you want to override all other status messages',
		:type  => Edit )

config.add(	:name  => 'message_avail',
		:message   => 'Set avaible status',
		:type  => Edit,
		:default => 'I\'m avaible' )


config.add(	:name  => 'message_free',
		:message   => 'Set free for chat status',
		:type  => Edit,
		:default => 'I\'m free for chat' )

config.add(	:name  => 'message_dnd',
		:message   => 'Set do not disturb status',
		:type  => Edit,
		:default => 'Please do not disturb' )

config.add(	:name  => 'message_notavail',
		:message   => 'Set not avaible status',
		:type  => Edit,
		:default => 'I\'m not avaible' )

config.add(	:name  => 'message_away',
		:message   => 'Set away status',
		:type  => Edit,
		:default => 'I\'m away' )

config.add(	:name  => 'message_autoaway',
		:msg   => 'Set auto-away status',
		:type  => Edit,
		:default => 'Auto-away' )

begin
  opts.each do |opt,arg|
    case opt
      when '--help'
        Wizzard.help()
      when '--version'
        Wizzard.version()
      when '--target'
        config.target          = arg
      when '--ignore'
        config.ignore_previous = true
      when '--ignore-auto'
        config.ignore_auto     = true
      when '--proxy'
        config.enqueue(proxy)
      when '--keep'
        config.enqueue('pinginterval')
      when '--tracelog'
        config.enqueue(tracelog)
      when '--status'
        config.enqueue(status)
      when '--nocolor'
        class String; @@color = false; end
    end
  end
rescue GetoptLong::InvalidOption
  Wizzard.help()
end

config.run
