module hello-car

go 1.22

require github.com/RobDeGeorge/OCTAVE/sdk/go v0.0.0

// Inside the OCTAVE checkout the SDK is two folders up; drop this line when you copy the folder out
replace github.com/RobDeGeorge/OCTAVE/sdk/go => ../../sdk/go
