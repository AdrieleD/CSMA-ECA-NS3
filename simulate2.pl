#!/usr/local/bin/perl
use List::Util qw(first max maxstr min minstr reduce shuffle sum);
my $nMax = $ARGV[1];
my $nMin = $ARGV[0];
my $rep = 1;
my $simulationTime = 10;
my $seed = -1; #Keep -1 to leave unchanged
my $stickiness = 0;

my $verbose = false;
my $eca = false;
my $hyst = false;
my $dynStick = false;
my $bitmap = false;
my @command = './waf --cwd=tmp2/ --run "scratch/simple-msdu-aggregation';

foreach (@ARGV){
	$eca = true
		if $_ eq '--eca';
	$hyst = true
		if $_ eq '--hyst';
	$bitmap = true
		if $_ eq '--bitmap';
	$verbose = true
		if $_ eq '--verbose';
	$dynStick = true
		if $_ eq '--dynStick';
}

if( $nMax > $nMin ){
	foreach my $i ($nMin..$nMax){
		foreach my $j (1 .. $rep){
			$seed = $j
				if($rep > 1);
			my @addition = ("        --nWifi=$i 
				--verbose=$verbose 
				--seed=$seed 
				--simulationTime=$simulationTime 
				--eca=$eca 
				--hysteresis=$hyst
				--stickiness=$stickiness 
				--dynStick=$dynStick										
				--bitmap=$bitmap\"");
			my @outPut = "@command @addition";
			print("###Simulating iteration $j of $rep\n");
			print ("@outPut\n");
			system(@outPut);
		}
	}
}else{
		die("More than one node, please\n")
			if($nMax < 1);
		foreach my $j (1 .. $rep){
			$seed = $j
				if($rep > 1);
			my @outPut = ("@command --nWifi=$nMin
				--verbose=$verbose 
				--seed=$seed 
				--simulationTime=$simulationTime 
				--eca=$eca 
				--hysteresis=$hyst
				--stickiness=$stickiness 
				--dynStick=$dynStick										
				--bitmap=$bitmap\"");
			print("###Simulating iteration $j of $rep\n");
			print ("@outPut\n");
			system(@outPut);
		}
}
