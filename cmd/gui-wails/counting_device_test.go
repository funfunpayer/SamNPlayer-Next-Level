package main

import "context"

// countingDevice zählt Steuerbefehle für den Connect-Smoke-Test.
type countingDevice struct {
	vib, suc int
	stopped  bool
}

func (c *countingDevice) Connect(context.Context) error { return nil }
func (c *countingDevice) Disconnect() error             { return nil }
func (c *countingDevice) SetVibration(float64) error {
	c.vib++
	return nil
}
func (c *countingDevice) SetSuction(float64) error {
	c.suc++
	return nil
}
func (c *countingDevice) Stop() error {
	c.stopped = true
	return nil
}
